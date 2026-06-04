#pragma once

#include <functional>
#include <string>
#include <vector>

#include "emberforge/tools/spec.hpp"

namespace emberforge::api {

struct MessageRequest {
    std::string model;
    std::string prompt;
    // Optional canonical agent system prompt. When non-empty, providers inject
    // it as a `system` message/block ahead of the user message. Empty by
    // default so existing 2-field aggregate initializers keep compiling.
    std::string system_prompt = {};
};

struct MessageResponse {
    std::string text;
};

// A single structured tool call requested by the model. Mirrors the Rust
// reference's `ContentBlock::ToolUse { id, name, input }`
// (crates/api/src/providers/mod.rs). `arguments` is the tool input object
// serialized as a JSON string — exactly what the ToolExecutor consumes — so the
// runtime never has to re-serialize before dispatch.
struct ToolCall {
    std::string id;         // provider-assigned id (Ollama often omits; may be empty)
    std::string name;       // tool name (must match a registry ToolSpec)
    std::string arguments;  // JSON object, serialized as a string
};

// One message in the accumulating conversation. `role` is "user" | "assistant"
// | "tool" (system is carried separately on ChatRequest, mirroring the Rust
// reference). Assistant turns that request tools carry `tool_calls`; tool-result
// turns carry the producing tool's name so tool-aware models can correlate.
struct ChatMessage {
    std::string role;
    std::string content;
    std::vector<ToolCall> tool_calls;  // populated on assistant turns
    std::string tool_name;             // set when role == "tool"
};

// Invoked with each incremental assistant text delta as it streams from the
// provider. Optional — when unset the provider still works, just buffered.
using TextDeltaSink = std::function<void(const std::string&)>;

// A single agentic chat turn. The provider is sent the whole accumulated
// `messages` list plus the native `tools` specs, and returns the assistant text
// AND any structured tool calls (see ChatResult). This is the structured
// provider result the multi-turn loop in the runtime drives.
struct ChatRequest {
    std::string model;                  // empty => provider's configured default
    std::string system_prompt;
    std::vector<ChatMessage> messages;
    std::vector<tools::ToolSpec> tools; // reused registry specs; empty => no tools array
    TextDeltaSink on_text_delta = {};   // optional streaming callback
};

struct ChatResult {
    std::string text;
    std::vector<ToolCall> tool_calls;
};

class Provider {
public:
    virtual ~Provider() = default;
    virtual MessageResponse send_message(const MessageRequest& request) = 0;

    // Structured, tool-aware chat turn driving the multi-turn agent loop.
    // The default implementation has NO native tool support: it forwards the
    // latest user message through send_message and returns the text with an
    // empty tool_calls list (so the loop terminates after one turn). Providers
    // that support native tool-calling (OllamaProvider) override this. Hosted
    // providers fall back to the default — honest single-turn behavior.
    virtual ChatResult chat(const ChatRequest& request);
};

class MockProvider final : public Provider {
public:
    MessageResponse send_message(const MessageRequest& request) override;
};

} // namespace emberforge::api
