#pragma once

#include <optional>
#include <string>

namespace emberforge::system {

struct ControlSequenceContext {
    std::string request_id;
    std::string input;
    std::optional<std::string> route;
};

} // namespace emberforge::system
