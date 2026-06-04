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

} // namespace emberforge::api
