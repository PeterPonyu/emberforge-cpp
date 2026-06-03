#include "emberforge/compat/upstream_paths.hpp"

namespace emberforge::compat {

UpstreamPaths default_upstream_paths() {
    // Neutral, brand-free provenance markers for the upstream reference
    // implementation. Values are intentionally generic module references rather
    // than another tool's source-tree layout; they are never surfaced to users.
    return {
        .upstream_commands_ref = "reference/commands",
        .upstream_tools_ref = "reference/tools",
        .upstream_runtime_ref = "reference/runtime",
    };
}

} // namespace emberforge::compat
