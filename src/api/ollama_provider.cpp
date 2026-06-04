#include "emberforge/api/ollama_provider.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>

namespace emberforge::api {

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

MessageResponse OllamaProvider::send_message(const MessageRequest& request) {
    const std::string& effective_model =
        request.model.empty() ? model_ : request.model;

    std::string escaped_prompt;
    for (char c : request.prompt) {
        switch (c) {
            case '"':  escaped_prompt += "\\\""; break;
            case '\\': escaped_prompt += "\\\\"; break;
            case '\n': escaped_prompt += "\\n";  break;
            case '\r': escaped_prompt += "\\r";  break;
            case '\t': escaped_prompt += "\\t";  break;
            default:   escaped_prompt += c;      break;
        }
    }

    const std::string body =
        "{\"model\":\"" + effective_model + "\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"" + escaped_prompt + "\"}],"
        "\"stream\":true}";

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
