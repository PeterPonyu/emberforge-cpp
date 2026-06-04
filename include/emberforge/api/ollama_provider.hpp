#pragma once

#include "emberforge/api/provider.hpp"
#include <string>

namespace emberforge::api {

// OllamaProvider sends chat requests to a local Ollama instance via HTTP POST
// to {base_url}/api/chat, consumes the NDJSON streaming response, and
// concatenates message.content deltas into MessageResponse.text.
//
// Env vars read by the factory helper in main.cpp:
//   OLLAMA_BASE_URL  — default: http://localhost:11434
//   EMBER_MODEL      — default: qwen3:8b
class OllamaProvider final : public Provider {
public:
    OllamaProvider(std::string base_url, std::string model);
    ~OllamaProvider() override = default;

    MessageResponse send_message(const MessageRequest& request) override;

    // Idempotently normalize an Ollama base URL: strip trailing '/' and a
    // single trailing "/v1" (OpenAI-compat) suffix so both http://HOST:PORT
    // and http://HOST:PORT/v1 resolve to the native API root. Generic — no
    // host/port assumptions. Exposed for unit testing.
    static std::string normalize_base_url(std::string base_url);

private:
    std::string base_url_;
    std::string model_;
};

} // namespace emberforge::api
