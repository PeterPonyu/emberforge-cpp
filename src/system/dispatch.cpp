#include "emberforge/system/dispatch.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace emberforge::system {

DispatchDecision SystemDispatcher::dispatch(const std::string& input) const {
    const auto trim = [](std::string value) {
        const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
        return value;
    };

    const auto trimmed = trim(input);
    if (trimmed.rfind("/tool ", 0) == 0) {
        return {
            .route = DispatchRoute::Tool,
            .payload = trimmed.substr(6),
            .command_name = std::nullopt,
            .tool_name = std::string{"bash"},
        };
    }

    if (!trimmed.empty() && trimmed.front() == '/') {
        const auto without_slash = trimmed.substr(1);
        const auto first_space = without_slash.find(' ');
        const auto command_name = without_slash.substr(0, first_space);
        const auto payload = first_space == std::string::npos ? std::string{} : trim(without_slash.substr(first_space + 1));
        return {
            .route = DispatchRoute::Command,
            .payload = payload,
            .command_name = command_name,
            .tool_name = std::nullopt,
        };
    }

    return {
        .route = DispatchRoute::Prompt,
        .payload = trimmed,
        .command_name = std::nullopt,
        .tool_name = std::nullopt,
    };
}

} // namespace emberforge::system
