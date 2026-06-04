#include "emberforge/api/ollama_provider.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>

namespace emberforge::api {

namespace {

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

static std::string extract_content_from_line(const std::string& line) {
    try {
        const auto obj = nlohmann::json::parse(line);
        return obj.value("/message/content"_json_pointer, std::string{});
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

static bool line_is_done(const std::string& line) {
    try {
        const auto obj = nlohmann::json::parse(line);
        return obj.value("done", false);
    } catch (const nlohmann::json::exception&) {
        return false;
    }
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
    std::string text;                    // accumulated assistant text
    std::vector<ToolCall> tool_calls;    // accumulated structured tool calls
    const TextDeltaSink* on_delta;       // optional, may be null
};

// Process one complete NDJSON line from the /api/chat stream.
static void process_chat_line(const std::string& line, ChatStreamContext& ctx) {
    nlohmann::json obj;
    try {
        obj = nlohmann::json::parse(line);
    } catch (const nlohmann::json::exception&) {
        return;  // ignore keep-alives / partial fragments
    }

    const std::string delta = obj.value("/message/content"_json_pointer, std::string{});
    if (!delta.empty()) {
        ctx.text += delta;
        if (ctx.on_delta && *ctx.on_delta) {
            (*ctx.on_delta)(delta);
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

    std::string accumulated;
    std::istringstream stream(raw_response);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        accumulated += extract_content_from_line(line);
        if (line_is_done(line)) break;
    }

    return MessageResponse{accumulated};
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

    return ChatResult{std::move(ctx.text), std::move(ctx.tool_calls)};
}

} // namespace emberforge::api
