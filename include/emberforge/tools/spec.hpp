#pragma once

#include <string>

namespace emberforge::tools {

// PermissionMode mirrors the Rust runtime::PermissionMode ordering
// (crates/runtime/src/permissions.rs). The numeric ordering is meaningful:
// a higher mode subsumes the capabilities of every lower mode, so a tool
// requiring ReadOnly is satisfied by any session running at WorkspaceWrite
// or DangerFullAccess.
enum class PermissionMode {
    ReadOnly = 0,
    WorkspaceWrite = 1,
    DangerFullAccess = 2,
};

// Human-readable label, matching the Rust as_str() spellings.
const char* to_string(PermissionMode mode);

// Returns true when a session operating at `current` is permitted to run a
// tool that declares `required` as its minimum permission.
bool permits(PermissionMode current, PermissionMode required);

struct ToolSpec {
    std::string name;
    std::string description;
    // JSON Schema describing the tool's input object, serialized as a JSON
    // string (parseable by nlohmann::json). Mirrors the `input_schema` field
    // of the Rust ToolSpec (crates/tools/src/specs.rs).
    std::string input_schema;
    PermissionMode required_permission;
};

} // namespace emberforge::tools
