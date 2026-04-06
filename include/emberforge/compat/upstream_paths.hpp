#pragma once

#include <string>

namespace emberforge::compat {

struct UpstreamPaths {
    std::string claude_commands_ts;
    std::string claude_tools_ts;
    std::string ember_runtime_lib_rs;
};

[[nodiscard]] UpstreamPaths default_upstream_paths();

} // namespace emberforge::compat
