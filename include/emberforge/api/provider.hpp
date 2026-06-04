#pragma once

#include <string>

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

class Provider {
public:
    virtual ~Provider() = default;
    virtual MessageResponse send_message(const MessageRequest& request) = 0;
};

class MockProvider final : public Provider {
public:
    MessageResponse send_message(const MessageRequest& request) override;
};

} // namespace emberforge::api
