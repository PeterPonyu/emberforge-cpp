#include "emberforge/api/provider.hpp"

namespace emberforge::api {

ChatResult Provider::chat(const ChatRequest& request) {
    // Default path for providers without native tool-calling: forward the most
    // recent user message through the single-turn send_message API and report
    // no tool calls, so the runtime's agent loop completes in one iteration.
    MessageRequest mr;
    mr.model = request.model;
    mr.system_prompt = request.system_prompt;
    for (auto it = request.messages.rbegin(); it != request.messages.rend(); ++it) {
        if (it->role == "user") {
            mr.prompt = it->content;
            break;
        }
    }

    MessageResponse resp = send_message(mr);
    if (request.on_text_delta && !resp.text.empty()) {
        request.on_text_delta(resp.text);
    }
    return ChatResult{resp.text, {}};
}

MessageResponse MockProvider::send_message(const MessageRequest& request) {
    return MessageResponse{
        "[cpp provider] model=" + request.model + " prompt=" + request.prompt,
    };
}

} // namespace emberforge::api
