#pragma once

#include <string>

namespace emberforge::api {

struct MessageRequest {
    std::string model;
    std::string prompt;
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
