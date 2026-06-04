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
// Env var read by this provider (see resolve_num_predict):
//   EMBER_OLLAMA_NUM_PREDICT — output-token bound override (default: model-aware,
//                              mirrors the Rust reference's max_tokens_for_model)
class OllamaProvider final : public Provider {
public:
    OllamaProvider(std::string base_url, std::string model);
    ~OllamaProvider() override = default;

    MessageResponse send_message(const MessageRequest& request) override;

    // Structured, tool-aware chat turn (the agentic core). Sends the whole
    // `messages` list plus the native `tools` array (built from the reused
    // registry specs) to /api/chat with stream:true, surfaces assistant text
    // deltas through request.on_text_delta as they arrive, and returns both the
    // accumulated text and any parsed `message.tool_calls`. Mirrors the Rust
    // reference's streaming + ToolUse extraction (crates/runtime/src/conversation.rs).
    ChatResult chat(const ChatRequest& request) override;

    // Idempotently normalize an Ollama base URL: strip trailing '/' and a
    // single trailing "/v1" (OpenAI-compat) suffix so both http://HOST:PORT
    // and http://HOST:PORT/v1 resolve to the native API root. Generic — no
    // host/port assumptions. Exposed for unit testing.
    static std::string normalize_base_url(std::string base_url);

    // Resolve the output-token bound (Ollama's native "num_predict" option)
    // for a model. Without a bound, thinking models (e.g. qwen3) generate
    // until natural stop, causing pathological latency; this caps generation
    // while staying generous enough that normal answers are never truncated.
    //
    // Precedence:
    //   1. EMBER_OLLAMA_NUM_PREDICT env var, if set to a positive integer.
    //   2. Model-aware default mirroring the Rust reference's
    //      max_tokens_for_model (crates/api/src/providers/mod.rs):
    //      32000 for "opus" models, 64000 otherwise.
    // Exposed for unit testing.
    static long resolve_num_predict(const std::string& model);

private:
    std::string base_url_;
    std::string model_;
};

} // namespace emberforge::api
