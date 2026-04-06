#include "emberforge/compat/upstream_paths.hpp"

namespace emberforge::compat {

UpstreamPaths default_upstream_paths() {
    return {
        .claude_commands_ts = "/home/zeyufu/Desktop/claude-code-src/commands.ts",
        .claude_tools_ts = "/home/zeyufu/Desktop/claude-code-src/tools.ts",
        .ember_runtime_lib_rs = "/home/zeyufu/Desktop/emberforge/crates/runtime/src/lib.rs",
    };
}

} // namespace emberforge::compat
