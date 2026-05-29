#include "emberforge/plugins/hook.hpp"

#include <algorithm>
#include <cctype>

namespace emberforge::plugins {

namespace {

std::string to_ascii_lower(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

} // namespace

bool glob_match(const std::string& pattern, const std::string& input) {
    // ASCII case-insensitive, mirroring crates/plugins/src/hooks.rs. A trailing
    // '*' means "input contains the prefix"; otherwise "input contains pattern".
    const std::string p = to_ascii_lower(pattern);
    const std::string in = to_ascii_lower(input);
    if (!p.empty() && p.back() == '*') {
        const std::string prefix = p.substr(0, p.size() - 1);
        if (prefix.empty()) {
            return true;  // bare "*" matches anything
        }
        return in.find(prefix) != std::string::npos;
    }
    return in.find(p) != std::string::npos;
}

bool HookMatchRule::matches(const std::string& tool_name,
                            const std::string& tool_input) const {
    // If tool_names is specified, the tool must be in the list.
    if (!tool_names.empty()) {
        const bool name_ok =
            std::any_of(tool_names.begin(), tool_names.end(),
                        [&](const std::string& name) { return name == tool_name; });
        if (!name_ok) {
            return false;
        }
    }
    // If command patterns are specified, the input must match at least one.
    if (!commands.empty()) {
        const bool cmd_ok =
            std::any_of(commands.begin(), commands.end(),
                        [&](const std::string& pattern) { return glob_match(pattern, tool_input); });
        if (!cmd_ok) {
            return false;
        }
    }
    return true;
}

} // namespace emberforge::plugins
