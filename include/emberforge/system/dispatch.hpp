#pragma once

#include <optional>
#include <string>

namespace emberforge::system {

enum class DispatchRoute {
    Command,
    Tool,
    Prompt,
};

[[nodiscard]] inline std::string to_string(DispatchRoute route) {
    switch (route) {
        case DispatchRoute::Command:
            return "command";
        case DispatchRoute::Tool:
            return "tool";
        case DispatchRoute::Prompt:
            return "prompt";
    }
    return "unknown";
}

struct DispatchDecision {
    DispatchRoute route;
    std::string payload;
    std::optional<std::string> command_name;
    std::optional<std::string> tool_name;
};

class SystemDispatcher {
public:
    [[nodiscard]] DispatchDecision dispatch(const std::string& input) const;
};

} // namespace emberforge::system
