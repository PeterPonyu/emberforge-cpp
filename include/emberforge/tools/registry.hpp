#pragma once

#include <optional>
#include <string>
#include <vector>

#include "emberforge/tools/spec.hpp"

namespace emberforge::tools {

class ToolExecutor {
public:
    virtual ~ToolExecutor() = default;
    virtual std::string execute(const std::string& tool_name, const std::string& input) = 0;
};

class MockToolExecutor final : public ToolExecutor {
public:
    std::string execute(const std::string& tool_name, const std::string& input) override;
};

// The canonical tool registry (EFPORT-7). Returns the full set of MVP tool
// specs — each with a name, description, JSON-Schema input definition, and the
// minimum PermissionMode required to invoke it. Mirrors the shape of the Rust
// mvp_tool_specs() in crates/tools/src/specs.rs.
std::vector<ToolSpec> get_tools();

// Looks up a single tool spec by exact name. Returns std::nullopt when the
// name is not registered.
std::optional<ToolSpec> find_tool(const std::string& name);

// PermissionToolExecutor wraps an inner ToolExecutor and enforces the
// registry's per-tool permission requirements before dispatching. A call is
// rejected (without touching the inner executor) when either the tool is not
// registered or the configured permission mode does not satisfy the tool's
// required_permission.
class PermissionToolExecutor final : public ToolExecutor {
public:
    PermissionToolExecutor(ToolExecutor& inner, PermissionMode mode);

    std::string execute(const std::string& tool_name, const std::string& input) override;

private:
    ToolExecutor& inner_;
    PermissionMode mode_;
};

} // namespace emberforge::tools
