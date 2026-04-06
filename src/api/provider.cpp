#include "emberforge/api/provider.hpp"

namespace emberforge::api {

MessageResponse MockProvider::send_message(const MessageRequest& request) {
    return MessageResponse{
        "[cpp provider] model=" + request.model + " prompt=" + request.prompt,
    };
}

} // namespace emberforge::api
