#include "emberforge/tools/registry.hpp"

#include <algorithm>

namespace emberforge::tools {

const char* to_string(PermissionMode mode) {
    switch (mode) {
        case PermissionMode::ReadOnly:        return "read-only";
        case PermissionMode::WorkspaceWrite:  return "workspace-write";
        case PermissionMode::DangerFullAccess: return "danger-full-access";
    }
    return "unknown";
}

bool permits(PermissionMode current, PermissionMode required) {
    return static_cast<int>(current) >= static_cast<int>(required);
}

std::vector<ToolSpec> get_tools() {
    // Each input_schema is a JSON-Schema object literal (as a string) mirroring
    // the Rust mvp_tool_specs() definitions in crates/tools/src/specs.rs.
    return {
        ToolSpec{
            "bash",
            "Execute a shell command in the current workspace.",
            R"({"type":"object","properties":{"command":{"type":"string"},)"
            R"("timeout":{"type":"integer","minimum":1},"description":{"type":"string"},)"
            R"("run_in_background":{"type":"boolean"},"dangerouslyDisableSandbox":{"type":"boolean"}},)"
            R"("required":["command"],"additionalProperties":false})",
            PermissionMode::DangerFullAccess,
        },
        ToolSpec{
            "read_file",
            "Read a text file from the workspace.",
            R"({"type":"object","properties":{"path":{"type":"string"},)"
            R"("offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1}},)"
            R"("required":["path"],"additionalProperties":false})",
            PermissionMode::ReadOnly,
        },
        ToolSpec{
            "write_file",
            "Write a text file in the workspace.",
            R"({"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},)"
            R"("required":["path","content"],"additionalProperties":false})",
            PermissionMode::WorkspaceWrite,
        },
        ToolSpec{
            "edit_file",
            "Replace text in a workspace file.",
            R"({"type":"object","properties":{"path":{"type":"string"},)"
            R"("old_string":{"type":"string"},"new_string":{"type":"string"},)"
            R"("replace_all":{"type":"boolean"}},)"
            R"("required":["path","old_string","new_string"],"additionalProperties":false})",
            PermissionMode::WorkspaceWrite,
        },
        ToolSpec{
            "glob_search",
            "Find files by glob pattern.",
            R"({"type":"object","properties":{"pattern":{"type":"string"},"path":{"type":"string"}},)"
            R"("required":["pattern"],"additionalProperties":false})",
            PermissionMode::ReadOnly,
        },
        ToolSpec{
            "grep_search",
            "Search file contents with a regex pattern.",
            R"({"type":"object","properties":{"pattern":{"type":"string"},"path":{"type":"string"},)"
            R"("glob":{"type":"string"},"output_mode":{"type":"string"},)"
            R"("-n":{"type":"boolean"},"-i":{"type":"boolean"},)"
            R"("head_limit":{"type":"integer","minimum":1}},)"
            R"("required":["pattern"],"additionalProperties":false})",
            PermissionMode::ReadOnly,
        },
        ToolSpec{
            "web",
            "Fetch a URL or search the web and return readable results.",
            R"({"type":"object","properties":{"url":{"type":"string","format":"uri"},)"
            R"("query":{"type":"string"},"prompt":{"type":"string"}},)"
            R"("additionalProperties":false})",
            PermissionMode::ReadOnly,
        },
        ToolSpec{
            "notebook",
            "Replace, insert, or delete a cell in a Jupyter notebook.",
            R"({"type":"object","properties":{"notebook_path":{"type":"string"},)"
            R"("cell_id":{"type":"string"},"new_source":{"type":"string"},)"
            R"("cell_type":{"type":"string","enum":["code","markdown"]},)"
            R"("edit_mode":{"type":"string","enum":["replace","insert","delete"]}},)"
            R"("required":["notebook_path"],"additionalProperties":false})",
            PermissionMode::WorkspaceWrite,
        },
        ToolSpec{
            "agent",
            "Launch a specialized agent task and persist its handoff metadata.",
            R"({"type":"object","properties":{"description":{"type":"string"},)"
            R"("prompt":{"type":"string"},"subagent_type":{"type":"string"},)"
            R"("name":{"type":"string"},"model":{"type":"string"}},)"
            R"("required":["description","prompt"],"additionalProperties":false})",
            PermissionMode::DangerFullAccess,
        },
        ToolSpec{
            "skill",
            "Load a local skill definition and its instructions.",
            R"({"type":"object","properties":{"skill":{"type":"string"},"args":{"type":"string"}},)"
            R"("required":["skill"],"additionalProperties":false})",
            PermissionMode::ReadOnly,
        },
    };
}

std::optional<ToolSpec> find_tool(const std::string& name) {
    const auto tools = get_tools();
    const auto it = std::find_if(tools.begin(), tools.end(),
                                 [&](const ToolSpec& spec) { return spec.name == name; });
    if (it == tools.end()) {
        return std::nullopt;
    }
    return *it;
}

PermissionToolExecutor::PermissionToolExecutor(ToolExecutor& inner, PermissionMode mode)
    : inner_(inner), mode_(mode) {}

std::string PermissionToolExecutor::execute(const std::string& tool_name,
                                            const std::string& input) {
    const auto spec = find_tool(tool_name);
    if (!spec) {
        return "[permission] error: unknown tool: " + tool_name;
    }
    if (!permits(mode_, spec->required_permission)) {
        return "[permission] error: tool '" + tool_name + "' requires " +
               to_string(spec->required_permission) + " but session is " +
               to_string(mode_);
    }
    return inner_.execute(tool_name, input);
}

std::string MockToolExecutor::execute(const std::string& tool_name, const std::string& input) {
    return "[cpp tool] " + tool_name + " => " + input;
}

} // namespace emberforge::tools
