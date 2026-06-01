#include "emberforge/api/hosted_provider.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace emberforge::api {

namespace {

std::size_t curl_write_callback(char* ptr, std::size_t size,
                                std::size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    const std::size_t total = size * nmemb;
    buf->append(ptr, total);
    return total;
}

std::string trim_trailing_slash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

// Performs a POST and returns the raw response body, throwing on transport or
// HTTP failure. Shared by both hosted providers.
std::string http_post(const std::string& url, const std::string& body,
                      const std::vector<std::string>& header_lines,
                      const char* provider_label) {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl{curl_easy_init(),
                                                              &curl_easy_cleanup};
    if (!curl) {
        throw std::runtime_error(std::string(provider_label) + ": curl_easy_init() failed");
    }

    std::string raw_response;
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers{nullptr,
                                                                         &curl_slist_free_all};
    for (const auto& line : header_lines) {
        headers.reset(curl_slist_append(headers.release(), line.c_str()));
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &raw_response);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);

    const CURLcode res = curl_easy_perform(curl.get());

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string(provider_label) +
                                 ": HTTP request failed: " + curl_easy_strerror(res));
    }
    return raw_response;
}

} // namespace

// ---------------------------------------------------------------------------
// AnthropicProvider
// ---------------------------------------------------------------------------

AnthropicProvider::AnthropicProvider(std::string api_key, std::string model,
                                     std::string base_url)
    : api_key_(std::move(api_key)),
      model_(std::move(model)),
      base_url_(trim_trailing_slash(std::move(base_url))) {}

std::string AnthropicProvider::endpoint() const {
    return base_url_ + "/v1/messages";
}

std::vector<std::string> AnthropicProvider::build_headers() const {
    return {
        std::string("content-type: application/json"),
        std::string("x-api-key: ") + api_key_,
        std::string("anthropic-version: ") + kAnthropicVersion,
        std::string("anthropic-beta: ") + kPromptCachingBeta,
    };
}

std::string AnthropicProvider::build_body(const MessageRequest& request,
                                          const std::string& system_prompt) const {
    const std::string effective_model = request.model.empty() ? model_ : request.model;

    nlohmann::json body;
    body["model"] = effective_model;
    body["max_tokens"] = 1024;
    body["stream"] = false;
    body["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"content", request.prompt}},
    });

    // Prompt caching: when a system prompt is present, emit it as a structured
    // block carrying a cache_control marker so the provider caches the prefix.
    if (!system_prompt.empty()) {
        body["system"] = nlohmann::json::array({
            nlohmann::json{
                {"type", "text"},
                {"text", system_prompt},
                {"cache_control", {{"type", "ephemeral"}}},
            },
        });
    }

    return body.dump();
}

MessageResponse AnthropicProvider::send_message(const MessageRequest& request) {
    const std::string raw =
        http_post(endpoint(), build_body(request), build_headers(), "AnthropicProvider");

    try {
        const auto obj = nlohmann::json::parse(raw);
        // Anthropic Messages API: content is an array of blocks; concatenate
        // the text blocks into the response text.
        std::string text;
        if (obj.contains("content") && obj["content"].is_array()) {
            for (const auto& block : obj["content"]) {
                if (block.value("type", std::string{}) == "text") {
                    text += block.value("text", std::string{});
                }
            }
        }
        return MessageResponse{text};
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("AnthropicProvider: invalid response: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// xAiProvider
// ---------------------------------------------------------------------------

xAiProvider::xAiProvider(std::string api_key, std::string model, std::string base_url)
    : api_key_(std::move(api_key)),
      model_(std::move(model)),
      base_url_(trim_trailing_slash(std::move(base_url))) {}

std::string xAiProvider::endpoint() const {
    // Mirrors chat_completions_endpoint(): append the suffix unless already present.
    static constexpr std::string_view kChatCompletionsSuffix = "/chat/completions";
    if (base_url_.size() >= kChatCompletionsSuffix.size() &&
        base_url_.compare(base_url_.size() - kChatCompletionsSuffix.size(),
                          kChatCompletionsSuffix.size(), kChatCompletionsSuffix) == 0) {
        return base_url_;
    }
    return base_url_ + std::string(kChatCompletionsSuffix);
}

std::vector<std::string> xAiProvider::build_headers() const {
    return {
        std::string("content-type: application/json"),
        std::string("Authorization: Bearer ") + api_key_,
    };
}

std::string xAiProvider::build_body(const MessageRequest& request,
                                    const std::string& system_prompt) const {
    const std::string effective_model = request.model.empty() ? model_ : request.model;

    nlohmann::json messages = nlohmann::json::array();
    if (!system_prompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", system_prompt}});
    }
    messages.push_back({{"role", "user"}, {"content", request.prompt}});

    nlohmann::json body;
    body["model"] = effective_model;
    body["messages"] = std::move(messages);
    body["stream"] = false;
    return body.dump();
}

MessageResponse xAiProvider::send_message(const MessageRequest& request) {
    const std::string raw =
        http_post(endpoint(), build_body(request), build_headers(), "xAiProvider");

    try {
        const auto obj = nlohmann::json::parse(raw);
        // OpenAI-compatible: choices[0].message.content
        std::string text =
            obj.value("/choices/0/message/content"_json_pointer, std::string{});
        return MessageResponse{text};
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("xAiProvider: invalid response: ") + e.what());
    }
}

} // namespace emberforge::api
