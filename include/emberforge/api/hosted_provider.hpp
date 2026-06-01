#pragma once

#include "emberforge/api/provider.hpp"

#include <string>
#include <vector>

namespace emberforge::api {

// AnthropicProvider talks to the Anthropic Messages API
// ({base_url}/v1/messages) over HTTP. It mirrors the Rust ClawApiClient
// (crates/api/src/providers/claw_provider.rs): x-api-key auth, the
// anthropic-version header, and prompt-caching enabled via the
// prompt-caching beta header plus a cache_control marker on the system block.
//
// Request construction (build_headers/build_body) is deliberately separated
// from the network call so it can be unit-tested offline.
class AnthropicProvider final : public Provider {
public:
    // anthropic-version header value sent with every request.
    static constexpr const char* kAnthropicVersion = "2023-06-01";
    // Beta header that opts the request into prompt caching.
    static constexpr const char* kPromptCachingBeta = "prompt-caching-2024-07-31";
    static constexpr const char* kDefaultBaseUrl = "https://api.anthropic.com";

    AnthropicProvider(std::string api_key, std::string model,
                      std::string base_url = kDefaultBaseUrl);
    ~AnthropicProvider() override = default;

    MessageResponse send_message(const MessageRequest& request) override;

    // Builds the JSON request body. When `system_prompt` is non-empty it is
    // emitted as a cache_control'd system block to engage prompt caching.
    [[nodiscard]] std::string build_body(const MessageRequest& request,
                                         const std::string& system_prompt = {}) const;

    // Builds the HTTP header lines (e.g. "x-api-key: ...") for the request.
    [[nodiscard]] std::vector<std::string> build_headers() const;

    [[nodiscard]] const std::string& model() const { return model_; }
    [[nodiscard]] std::string endpoint() const;

private:
    std::string api_key_;
    std::string model_;
    std::string base_url_;
};

// xAiProvider talks to the xAI Grok API via the OpenAI-compatible
// /chat/completions endpoint, mirroring the Rust OpenAiCompatClient
// configured with OpenAiCompatConfig::xai()
// (crates/api/src/providers/openai_compat.rs): bearer auth on XAI_API_KEY.
class xAiProvider final : public Provider {
public:
    static constexpr const char* kDefaultBaseUrl = "https://api.x.ai/v1";

    xAiProvider(std::string api_key, std::string model,
                std::string base_url = kDefaultBaseUrl);
    ~xAiProvider() override = default;

    MessageResponse send_message(const MessageRequest& request) override;

    [[nodiscard]] std::string build_body(const MessageRequest& request,
                                         const std::string& system_prompt = {}) const;
    [[nodiscard]] std::vector<std::string> build_headers() const;

    [[nodiscard]] const std::string& model() const { return model_; }
    [[nodiscard]] std::string endpoint() const;

private:
    std::string api_key_;
    std::string model_;
    std::string base_url_;
};

} // namespace emberforge::api
