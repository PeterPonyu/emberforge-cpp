#pragma once

#include "emberforge/api/provider.hpp"
#include <string>
#include <vector>

namespace emberforge::api {

// NAMED env flag controlling whether a thinking model's reasoning is surfaced
// (to stderr). Default OFF: stdout always carries the final answer only. Mirrors
// the Rust reference's separate-section treatment and the Go/TS ports'
// EMBER_SHOW_THINKING flag. Truthy values: 1/true/yes/on (case-insensitive).
inline constexpr const char* kShowThinkingEnv = "EMBER_SHOW_THINKING";

// Reports whether thinking content should be made visible, reading the
// EMBER_SHOW_THINKING env flag. Off by default. Exposed for testing.
[[nodiscard]] bool show_thinking();

// Streaming separator that splits a thinking model's output into the final
// ANSWER and its reasoning (THINKING). Mirrors the Rust reference's
// separate-channel handling and the Go/TS thinkSplitter:
//   - Structured reasoning (Ollama's `message.thinking` in think:true mode) is
//     accumulated verbatim via add_structured_thinking and never reaches the
//     answer.
//   - A single well-formed LEADING `<think>...</think>` block in `message.content`
//     is stripped (the inline fallback some models use). Content is only treated
//     as thinking when it *starts* with `<think>`; a legitimate later `<think>`
//     mention is left untouched (no regex-mangling — a proper state machine over
//     tag boundaries, holding back partial tags split across stream chunks).
// push_content returns only the answer text safe to emit so far; finish flushes
// any buffered tail at stream end.
class ThinkStreamSeparator {
public:
    // Feed a content delta; returns the answer text to emit now (may be empty).
    [[nodiscard]] std::string push_content(const std::string& delta);

    // Accumulate a structured `message.thinking` delta (preferred channel).
    void add_structured_thinking(const std::string& delta);

    // Flush any buffered content at stream end; returns the final answer tail.
    [[nodiscard]] std::string finish();

    // The accumulated reasoning content (answer-free).
    [[nodiscard]] const std::string& thinking_text() const { return thinking_; }

private:
    enum class State { Detecting, Thinking, Answer };
    State state_ = State::Detecting;
    std::string pending_;
    std::string thinking_;
};

// Apply the separator to a complete content string in one pass, returning the
// clean answer; the stripped reasoning is written to `thinking` (appended).
// Convenience for the non-streaming send_message path. Exposed for testing.
[[nodiscard]] std::string strip_leading_think_block(const std::string& content,
                                                    std::string& thinking);

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

    // List the local model tags this Ollama instance serves, via the native
    // `GET {base}/api/tags`. Returns the sorted, de-duplicated tag names.
    // Throws std::runtime_error on transport/HTTP failure so the caller can
    // surface an "unreachable" status (mirrors the Rust list_ollama_models).
    [[nodiscard]] std::vector<std::string> list_models() const;

    // Parse an Ollama `/api/tags` JSON payload into the sorted, de-duplicated
    // model-tag list. Pure and fixture-testable (no network). Tolerates a
    // missing/!array `models` field by returning an empty list.
    [[nodiscard]] static std::vector<std::string> parse_tags_response(
        const std::string& json_body);

    // The active model used for turns when a request does not override it.
    // Plumbed so `/model <name>` can switch the model for subsequent turns.
    void set_model(const std::string& model) override { model_ = model; }
    [[nodiscard]] std::string current_model() const override { return model_; }

    // The normalized base URL this provider targets (exposed for model listing).
    [[nodiscard]] const std::string& base_url() const { return base_url_; }

private:
    std::string base_url_;
    std::string model_;
};

} // namespace emberforge::api
