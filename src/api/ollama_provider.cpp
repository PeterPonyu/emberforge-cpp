#include "emberforge/api/ollama_provider.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>

namespace emberforge::api {

namespace {

// Tags potentially split across NDJSON chunks; the separator holds back a
// partial trailing tag so it is never misclassified.
constexpr std::string_view kThinkOpen = "<think>";
constexpr std::string_view kThinkClose = "</think>";

// Longest suffix of `s` that is a strict (non-full) prefix of `tag` — the number
// of trailing chars that could be the start of a split tag and must be held
// back. Mirrors the Go/TS partialTagSuffixLen / partialMarkerSuffix.
std::size_t partial_tag_suffix_len(const std::string& s, std::string_view tag) {
    const std::size_t max_len = std::min(tag.size() - 1, s.size());
    for (std::size_t n = max_len; n > 0; --n) {
        if (tag.substr(0, n) == std::string_view(s).substr(s.size() - n)) {
            return n;
        }
    }
    return 0;
}

// Model families that emit a separate reasoning channel, mirroring the Rust
// reference's THINKING_FAMILIES and the Go/TS ports. For these we request
// Ollama's structured `think` mode so reasoning arrives in message.thinking.
constexpr std::array<std::string_view, 2> kThinkingFamilies = {"qwen3", "deepseek-r1"};

// Whether `model` is a known thinking model (case-insensitive family prefix).
bool is_thinking_model(const std::string& model) {
    std::string lower;
    lower.reserve(model.size());
    for (char c : model) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto family : kThinkingFamilies) {
        if (lower.rfind(family, 0) == 0) {
            return true;
        }
    }
    return false;
}

// Left-trim ASCII whitespace.
std::string ltrim(const std::string& s) {
    const std::size_t begin = s.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? std::string{} : s.substr(begin);
}

// Interpret an env value as a boolean flag: empty/unset is false; "1", "true",
// "yes", "on" (case-insensitive) are true.
bool env_flag_enabled(const char* raw) {
    if (raw == nullptr) {
        return false;
    }
    std::string lower;
    for (char c : std::string(raw)) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

// Surface separated reasoning to stderr when show_thinking() is enabled. Kept
// provider-level so both REPL and one-shot paths benefit and stdout stays the
// answer only.
void emit_thinking(const std::string& thinking) {
    const std::size_t begin = thinking.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos || !show_thinking()) {
        return;
    }
    const std::size_t end = thinking.find_last_not_of(" \t\r\n");
    std::cerr << "[thinking] " << thinking.substr(begin, end - begin + 1) << '\n';
}

// Output-token bound defaults. These mirror the Rust reference's
// max_tokens_for_model (crates/api/src/providers/mod.rs): a generous ceiling so
// normal answers are never truncated, while bounding the unbounded "thinking"
// generation of models like qwen3 that otherwise drives pathological latency.
// They are NOT buried magic literals: named, documented, and overridable via
// EMBER_OLLAMA_NUM_PREDICT (see OllamaProvider::resolve_num_predict).
constexpr long kDefaultNumPredict = 64000;  // non-opus default (reference parity)
constexpr long kOpusNumPredict    = 32000;  // opus default (reference parity)
constexpr const char* kNumPredictEnv = "EMBER_OLLAMA_NUM_PREDICT";

} // namespace

static std::size_t curl_write_callback(char* ptr, std::size_t size,
                                       std::size_t nmemb, void* userdata);

bool show_thinking() {
    return env_flag_enabled(std::getenv(kShowThinkingEnv));
}

std::string ThinkStreamSeparator::push_content(const std::string& delta) {
    pending_ += delta;
    std::string emit;
    for (;;) {
        switch (state_) {
            case State::Detecting: {
                const std::string lstripped = ltrim(pending_);
                if (lstripped.empty()) {
                    // Only leading whitespace so far — could precede <think>.
                    return emit;
                }
                if (lstripped.rfind(kThinkOpen, 0) == 0) {
                    // Well-formed leading think block: drop leading WS + open tag.
                    pending_ = lstripped.substr(kThinkOpen.size());
                    state_ = State::Thinking;
                    continue;
                }
                if (std::string(kThinkOpen).rfind(lstripped, 0) == 0) {
                    // `lstripped` is a partial prefix of "<think>" — wait for more.
                    return emit;
                }
                // No leading think block: everything (incl. leading WS) is answer.
                emit += pending_;
                pending_.clear();
                state_ = State::Answer;
                return emit;
            }
            case State::Thinking: {
                const std::size_t idx = pending_.find(kThinkClose);
                if (idx != std::string::npos) {
                    thinking_ += pending_.substr(0, idx);
                    pending_ = pending_.substr(idx + kThinkClose.size());
                    // Drop a single newline right after the close tag so the
                    // answer doesn't start with a stray blank line.
                    if (!pending_.empty() && pending_.front() == '\n') {
                        pending_.erase(pending_.begin());
                    }
                    state_ = State::Answer;
                    continue;
                }
                // No close tag yet: emit thinking but hold back a possible
                // partial closing tag at the tail.
                const std::size_t hold = partial_tag_suffix_len(pending_, kThinkClose);
                if (hold < pending_.size()) {
                    thinking_ += pending_.substr(0, pending_.size() - hold);
                    pending_ = pending_.substr(pending_.size() - hold);
                }
                return emit;
            }
            case State::Answer:
            default: {
                emit += pending_;
                pending_.clear();
                return emit;
            }
        }
    }
}

void ThinkStreamSeparator::add_structured_thinking(const std::string& delta) {
    thinking_ += delta;
}

std::string ThinkStreamSeparator::finish() {
    if (state_ == State::Thinking) {
        // Unterminated leading think block → the remainder is all reasoning.
        thinking_ += pending_;
        pending_.clear();
        return {};
    }
    // Detecting (never matched a full <think>) or answer → remainder is answer.
    std::string out = std::move(pending_);
    pending_.clear();
    state_ = State::Answer;
    return out;
}

std::string strip_leading_think_block(const std::string& content,
                                      std::string& thinking) {
    ThinkStreamSeparator separator;
    std::string answer = separator.push_content(content);
    answer += separator.finish();
    thinking += separator.thinking_text();
    return answer;
}

std::vector<std::string> OllamaProvider::parse_tags_response(
    const std::string& json_body) {
    std::set<std::string> names;
    try {
        const auto obj = nlohmann::json::parse(json_body);
        if (obj.contains("models") && obj["models"].is_array()) {
            for (const auto& model : obj["models"]) {
                const std::string name = model.value("name", std::string{});
                const std::size_t begin = name.find_first_not_of(" \t\r\n");
                if (begin != std::string::npos) {
                    names.insert(name);
                }
            }
        }
    } catch (const nlohmann::json::exception&) {
        return {};
    }
    return std::vector<std::string>(names.begin(), names.end());
}

std::vector<std::string> OllamaProvider::list_models() const {
    const std::string url = base_url_ + "/api/tags";

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("OllamaProvider: curl_easy_init() failed");
    }

    std::string raw_response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(
            std::string("OllamaProvider: /api/tags request failed: ") +
            curl_easy_strerror(res));
    }
    return parse_tags_response(raw_response);
}

static std::size_t curl_write_callback(char* ptr, std::size_t size,
                                       std::size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    const std::size_t total = size * nmemb;
    buf->append(ptr, total);
    return total;
}

// Convert the reused registry tool specs into Ollama's native `tools` array.
// Each entry is {"type":"function","function":{name, description, parameters}}
// where `parameters` is the spec's JSON-Schema input definition parsed back
// into an object. Reuses the existing registry — no hand-written schemas.
static nlohmann::json build_tools_array(const std::vector<tools::ToolSpec>& specs) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& spec : specs) {
        nlohmann::json parameters;
        try {
            parameters = nlohmann::json::parse(spec.input_schema);
        } catch (const nlohmann::json::exception&) {
            parameters = nlohmann::json::object();
        }
        arr.push_back({
            {"type", "function"},
            {"function", {
                {"name", spec.name},
                {"description", spec.description},
                {"parameters", std::move(parameters)},
            }},
        });
    }
    return arr;
}

// Serialize the accumulated conversation into Ollama's `messages` array. The
// system prompt (when present) is injected as the leading `system` message,
// mirroring send_message and the Rust reference's composite agent framing.
static nlohmann::json build_messages_array(const ChatRequest& request) {
    nlohmann::json messages = nlohmann::json::array();
    if (!request.system_prompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", request.system_prompt}});
    }
    for (const auto& msg : request.messages) {
        nlohmann::json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        if (!msg.tool_calls.empty()) {
            nlohmann::json tcs = nlohmann::json::array();
            for (const auto& tc : msg.tool_calls) {
                nlohmann::json arguments;
                try {
                    arguments = nlohmann::json::parse(tc.arguments);
                } catch (const nlohmann::json::exception&) {
                    arguments = tc.arguments;  // preserve raw if not valid JSON
                }
                nlohmann::json one = {{"function", {{"name", tc.name},
                                                    {"arguments", std::move(arguments)}}}};
                if (!tc.id.empty()) {
                    one["id"] = tc.id;
                }
                tcs.push_back(std::move(one));
            }
            m["tool_calls"] = std::move(tcs);
        }
        if (msg.role == "tool" && !msg.tool_name.empty()) {
            m["tool_name"] = msg.tool_name;
        }
        messages.push_back(std::move(m));
    }
    return messages;
}

// Streaming context threaded through the libcurl write callback so NDJSON lines
// are parsed AS THEY ARRIVE: text deltas are appended (and surfaced via the
// optional sink) and any message.tool_calls are accumulated.
struct ChatStreamContext {
    std::string partial;                 // buffer for an incomplete trailing line
    std::string text;                    // accumulated assistant ANSWER text
    std::vector<ToolCall> tool_calls;    // accumulated structured tool calls
    const TextDeltaSink* on_delta;       // optional, may be null
    // Splits each content delta into the final answer and the model's reasoning,
    // so only the answer is streamed/accumulated; a well-formed leading
    // <think> block and structured message.thinking are held off-band.
    ThinkStreamSeparator separator;
};

// Process one complete NDJSON line from the /api/chat stream.
static void process_chat_line(const std::string& line, ChatStreamContext& ctx) {
    nlohmann::json obj;
    try {
        obj = nlohmann::json::parse(line);
    } catch (const nlohmann::json::exception&) {
        return;  // ignore keep-alives / partial fragments
    }

    if (obj.contains("message") && obj["message"].is_object()) {
        const auto& message = obj["message"];
        // Structured reasoning channel (think:true models) — never an answer.
        const std::string thinking = message.value("thinking", std::string{});
        if (!thinking.empty()) {
            ctx.separator.add_structured_thinking(thinking);
        }
    }

    const std::string delta = obj.value("/message/content"_json_pointer, std::string{});
    if (!delta.empty()) {
        // Stream only the ANSWER portion; the separator strips a leading
        // <think>...</think> block so reasoning never reaches stdout.
        const std::string answer = ctx.separator.push_content(delta);
        if (!answer.empty()) {
            ctx.text += answer;
            if (ctx.on_delta && *ctx.on_delta) {
                (*ctx.on_delta)(answer);
            }
        }
    }

    if (obj.contains("message") && obj["message"].is_object()) {
        const auto& message = obj["message"];
        if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
            for (const auto& tc : message["tool_calls"]) {
                ToolCall call;
                call.id = tc.value("id", std::string{});
                if (tc.contains("function") && tc["function"].is_object()) {
                    const auto& fn = tc["function"];
                    call.name = fn.value("name", std::string{});
                    // Ollama emits arguments as a JSON object; serialize it back
                    // to the string the ToolExecutor consumes. Tolerate a string.
                    if (fn.contains("arguments")) {
                        call.arguments = fn["arguments"].is_string()
                                             ? fn["arguments"].get<std::string>()
                                             : fn["arguments"].dump();
                    }
                }
                if (!call.name.empty()) {
                    ctx.tool_calls.push_back(std::move(call));
                }
            }
        }
    }
}

static std::size_t chat_stream_callback(char* ptr, std::size_t size,
                                        std::size_t nmemb, void* userdata) {
    auto* ctx = static_cast<ChatStreamContext*>(userdata);
    const std::size_t total = size * nmemb;
    ctx->partial.append(ptr, total);

    std::size_t newline_pos = 0;
    while ((newline_pos = ctx->partial.find('\n')) != std::string::npos) {
        const std::string line = ctx->partial.substr(0, newline_pos);
        ctx->partial.erase(0, newline_pos + 1);
        if (!line.empty()) {
            process_chat_line(line, *ctx);
        }
    }
    return total;
}

// Normalize an Ollama base URL idempotently so that both the root form
// (http://HOST:PORT) and the OpenAI-compatible form (http://HOST:PORT/v1)
// resolve to the same native API root. We strip any trailing '/' characters
// and a single trailing "/v1" segment (the OpenAI-compat suffix), since the
// native endpoint path "/api/chat" is appended to the result. This is generic:
// no host or port is assumed, and existing root-form configs are unchanged.
std::string OllamaProvider::normalize_base_url(std::string base_url) {
    auto strip_trailing_slashes = [](std::string& s) {
        while (!s.empty() && s.back() == '/') {
            s.pop_back();
        }
    };

    strip_trailing_slashes(base_url);
    constexpr std::string_view v1_suffix = "/v1";
    if (base_url.size() >= v1_suffix.size() &&
        base_url.compare(base_url.size() - v1_suffix.size(),
                         v1_suffix.size(), v1_suffix) == 0) {
        base_url.erase(base_url.size() - v1_suffix.size());
        strip_trailing_slashes(base_url);
    }
    return base_url;
}

OllamaProvider::OllamaProvider(std::string base_url, std::string model)
    : base_url_(normalize_base_url(std::move(base_url))), model_(std::move(model)) {}

long OllamaProvider::resolve_num_predict(const std::string& model) {
    // 1. Explicit operator override via env var (positive integer only).
    if (const char* raw = std::getenv(kNumPredictEnv); raw != nullptr) {
        try {
            const long parsed = std::stol(raw);
            if (parsed > 0) {
                return parsed;
            }
        } catch (const std::exception&) {
            // Malformed value: fall through to the model-aware default.
        }
    }

    // 2. Model-aware default mirroring the Rust reference's max_tokens_for_model.
    std::string lower;
    lower.reserve(model.size());
    for (char c : model) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower.find("opus") != std::string::npos) {
        return kOpusNumPredict;
    }
    return kDefaultNumPredict;
}

MessageResponse OllamaProvider::send_message(const MessageRequest& request) {
    const std::string& effective_model =
        request.model.empty() ? model_ : request.model;

    // Bound output generation so thinking models cannot run unbounded. The
    // value is configurable (EMBER_OLLAMA_NUM_PREDICT) with a generous,
    // documented default — not a magic literal embedded in the JSON below.
    const long num_predict = resolve_num_predict(effective_model);

    // Build the request with nlohmann::json so the (potentially large)
    // canonical system prompt and the user prompt are escaped correctly. When
    // a system prompt is present it is injected as a `system` message ahead of
    // the user message, mirroring the Rust reference's composite agent framing.
    nlohmann::json messages = nlohmann::json::array();
    if (!request.system_prompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", request.system_prompt}});
    }
    messages.push_back({{"role", "user"}, {"content", request.prompt}});

    nlohmann::json body_json;
    body_json["model"] = effective_model;
    body_json["messages"] = std::move(messages);
    body_json["options"] = {{"num_predict", num_predict}};
    body_json["stream"] = true;
    // Thinking models route reasoning into the structured message.thinking
    // channel; the separator also strips any inline leading <think> block.
    if (is_thinking_model(effective_model)) {
        body_json["think"] = true;
    }
    const std::string body = body_json.dump();

    const std::string url = base_url_ + "/api/chat";

    // Initialise libcurl (thread-safe since curl_easy_init is per-handle).
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("OllamaProvider: curl_easy_init() failed");
    }

    std::string raw_response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_response);
    // Do not follow redirects; Ollama is local.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    // Fail explicitly on HTTP 4xx/5xx so we can surface errors.
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    const CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(
            std::string("OllamaProvider: HTTP request failed: ") +
            curl_easy_strerror(res));
    }

    // Separate the model's reasoning from its final answer: structured
    // `message.thinking` (think:true models) is accumulated off-band, and a
    // well-formed LEADING <think>...</think> block inlined into content is
    // stripped. stdout (this return value) carries the answer only; reasoning
    // goes to stderr behind EMBER_SHOW_THINKING (default off).
    ThinkStreamSeparator separator;
    std::string answer;
    std::istringstream stream(raw_response);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        try {
            const auto obj = nlohmann::json::parse(line);
            if (obj.contains("message") && obj["message"].is_object()) {
                const auto& message = obj["message"];
                const std::string thinking = message.value("thinking", std::string{});
                if (!thinking.empty()) {
                    separator.add_structured_thinking(thinking);
                }
                const std::string content = message.value("content", std::string{});
                if (!content.empty()) {
                    answer += separator.push_content(content);
                }
            }
            if (obj.value("done", false)) break;
        } catch (const nlohmann::json::exception&) {
            // ignore keep-alives / partial fragments
        }
    }
    answer += separator.finish();
    emit_thinking(separator.thinking_text());

    return MessageResponse{answer};
}

ChatResult OllamaProvider::chat(const ChatRequest& request) {
    const std::string& effective_model =
        request.model.empty() ? model_ : request.model;

    const long num_predict = resolve_num_predict(effective_model);

    nlohmann::json body_json;
    body_json["model"] = effective_model;
    body_json["messages"] = build_messages_array(request);
    body_json["options"] = {{"num_predict", num_predict}};
    body_json["stream"] = true;
    // Thinking models route reasoning into the structured message.thinking
    // channel; the separator also strips any inline leading <think> block so the
    // streamed answer (and tool-call turns) never leak reasoning.
    if (is_thinking_model(effective_model)) {
        body_json["think"] = true;
    }
    // Native tool-calling: advertise the reused registry specs so the model can
    // emit structured message.tool_calls (no string-scraping). Omit when empty.
    if (!request.tools.empty()) {
        body_json["tools"] = build_tools_array(request.tools);
    }
    const std::string body = body_json.dump();

    const std::string url = base_url_ + "/api/chat";

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("OllamaProvider: curl_easy_init() failed");
    }

    ChatStreamContext ctx;
    ctx.on_delta = &request.on_text_delta;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, chat_stream_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    const CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(
            std::string("OllamaProvider: HTTP request failed: ") +
            curl_easy_strerror(res));
    }

    // Drain any trailing line that did not end in a newline.
    if (!ctx.partial.empty()) {
        process_chat_line(ctx.partial, ctx);
    }

    // Flush any answer tail buffered by the separator, then surface the
    // separated reasoning to stderr (behind EMBER_SHOW_THINKING; default off).
    const std::string tail = ctx.separator.finish();
    if (!tail.empty()) {
        ctx.text += tail;
        if (ctx.on_delta && *ctx.on_delta) {
            (*ctx.on_delta)(tail);
        }
    }
    emit_thinking(ctx.separator.thinking_text());

    return ChatResult{std::move(ctx.text), std::move(ctx.tool_calls)};
}

} // namespace emberforge::api
