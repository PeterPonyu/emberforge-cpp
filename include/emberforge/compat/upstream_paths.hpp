#pragma once

#include <string>

namespace emberforge::compat {

// Internal compat-provenance references to the upstream reference implementation.
// These are non-shipping constants: they are not printed in any user-facing
// output and exist only to document parity with the reference port.
struct UpstreamPaths {
    std::string upstream_commands_ref;
    std::string upstream_tools_ref;
    std::string upstream_runtime_ref;
};

[[nodiscard]] UpstreamPaths default_upstream_paths();

} // namespace emberforge::compat
