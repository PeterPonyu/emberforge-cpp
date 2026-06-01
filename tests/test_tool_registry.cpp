// test_tool_registry.cpp
//
// Unit tests for the full tool registry (EFPORT-7): spec presence, JSON-Schema
// validity, per-tool permission requirements, permission-routed dispatch, and
// the new local executors (edit_file / glob_search / grep_search).
//
// No external test framework — plain checks and a failure counter.

#include "emberforge/tools/real_executor.hpp"
#include "emberforge/tools/registry.hpp"
#include "emberforge/tools/spec.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "PASS (" << name << ")\n";
    } else {
        std::cerr << "FAIL (" << name << ")\n";
        ++g_failures;
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool has_tool(const std::vector<emberforge::tools::ToolSpec>& tools, const std::string& name) {
    return std::any_of(tools.begin(), tools.end(),
                       [&](const auto& spec) { return spec.name == name; });
}

} // namespace

int main() {
    using namespace emberforge::tools;

    // ------------------------------------------------------------------
    // Registry contains every required tool.
    // ------------------------------------------------------------------
    {
        const auto tools = get_tools();
        for (const char* expected :
             {"bash", "read_file", "write_file", "edit_file", "glob_search",
              "grep_search", "web", "notebook", "agent", "skill"}) {
            check(has_tool(tools, expected), std::string("registry_has_") + expected);
        }
        check(tools.size() == 10, "registry_size_is_ten");
    }

    // ------------------------------------------------------------------
    // Every spec has a parseable JSON-Schema object input_schema.
    // ------------------------------------------------------------------
    {
        bool all_valid = true;
        for (const auto& spec : get_tools()) {
            try {
                const auto schema = nlohmann::json::parse(spec.input_schema);
                if (!schema.is_object() || schema.value("type", std::string{}) != "object") {
                    all_valid = false;
                }
            } catch (const nlohmann::json::exception&) {
                all_valid = false;
            }
        }
        check(all_valid, "all_input_schemas_are_valid_json_objects");
    }

    // ------------------------------------------------------------------
    // Permission requirements mirror the Rust specs.
    // ------------------------------------------------------------------
    {
        check(find_tool("bash")->required_permission == PermissionMode::DangerFullAccess,
              "bash_requires_danger_full_access");
        check(find_tool("read_file")->required_permission == PermissionMode::ReadOnly,
              "read_file_requires_read_only");
        check(find_tool("write_file")->required_permission == PermissionMode::WorkspaceWrite,
              "write_file_requires_workspace_write");
        check(find_tool("edit_file")->required_permission == PermissionMode::WorkspaceWrite,
              "edit_file_requires_workspace_write");
        check(find_tool("glob_search")->required_permission == PermissionMode::ReadOnly,
              "glob_search_requires_read_only");
        check(find_tool("agent")->required_permission == PermissionMode::DangerFullAccess,
              "agent_requires_danger_full_access");
        check(!find_tool("nonexistent").has_value(), "find_tool_unknown_is_nullopt");
    }

    // ------------------------------------------------------------------
    // permits(): ordering semantics.
    // ------------------------------------------------------------------
    {
        check(permits(PermissionMode::DangerFullAccess, PermissionMode::ReadOnly),
              "danger_permits_read_only");
        check(permits(PermissionMode::WorkspaceWrite, PermissionMode::ReadOnly),
              "workspace_write_permits_read_only");
        check(!permits(PermissionMode::ReadOnly, PermissionMode::WorkspaceWrite),
              "read_only_denies_workspace_write");
        check(!permits(PermissionMode::WorkspaceWrite, PermissionMode::DangerFullAccess),
              "workspace_write_denies_danger");
    }

    // ------------------------------------------------------------------
    // PermissionToolExecutor routing: allow when permitted, reject otherwise.
    // ------------------------------------------------------------------
    {
        RealToolExecutor real;

        // read-only session may read but not write/bash.
        PermissionToolExecutor ro(real, PermissionMode::ReadOnly);
        const std::string write_denied =
            ro.execute("write_file", R"({"path":"x.txt","content":"y"})");
        check(contains(write_denied, "[permission] error") &&
                  contains(write_denied, "workspace-write"),
              "read_only_session_denied_write");
        const std::string bash_denied = ro.execute("bash", R"({"command":"echo hi"})");
        check(contains(bash_denied, "[permission] error") &&
                  contains(bash_denied, "danger-full-access"),
              "read_only_session_denied_bash");

        // danger session may run bash through the permission wrapper.
        PermissionToolExecutor danger(real, PermissionMode::DangerFullAccess);
        const std::string bash_ok = danger.execute("bash", R"({"command":"echo permitted"})");
        check(contains(bash_ok, "permitted"), "danger_session_runs_bash");

        // unknown tool rejected before dispatch.
        const std::string unknown = danger.execute("does_not_exist", "{}");
        check(contains(unknown, "[permission] error") && contains(unknown, "unknown tool"),
              "unknown_tool_rejected");
    }

    // ------------------------------------------------------------------
    // RealToolExecutor: edit_file replaces unique occurrence.
    // ------------------------------------------------------------------
    {
        RealToolExecutor real;
        const std::string path = "emberforge_test_edit.txt";
        {
            std::ofstream ofs(path);
            ofs << "alpha BETA gamma\n";
        }
        const std::string result =
            real.execute("edit_file",
                         R"({"path":"emberforge_test_edit.txt","old_string":"BETA","new_string":"delta"})");
        std::ifstream ifs(path);
        const std::string content((std::istreambuf_iterator<char>(ifs)),
                                  std::istreambuf_iterator<char>());
        std::filesystem::remove(path);
        check(contains(result, "[edit_file] ok") && content == "alpha delta gamma\n",
              "edit_file_replaces_unique_match");
    }

    // ------------------------------------------------------------------
    // RealToolExecutor: edit_file rejects ambiguous (non-unique) match.
    // ------------------------------------------------------------------
    {
        RealToolExecutor real;
        const std::string path = "emberforge_test_edit_dup.txt";
        {
            std::ofstream ofs(path);
            ofs << "dup dup dup";
        }
        const std::string result =
            real.execute("edit_file",
                         R"({"path":"emberforge_test_edit_dup.txt","old_string":"dup","new_string":"x"})");
        std::filesystem::remove(path);
        check(contains(result, "not unique"), "edit_file_rejects_ambiguous_match");
    }

    // ------------------------------------------------------------------
    // RealToolExecutor: glob_search and grep_search over a temp tree.
    // ------------------------------------------------------------------
    {
        RealToolExecutor real;
        const std::string dir = "emberforge_test_search_dir";
        std::filesystem::create_directories(dir);
        {
            std::ofstream a(dir + "/one.txt");
            a << "needle here\nplain line\n";
            std::ofstream b(dir + "/two.log");
            b << "no match in this file\n";
        }

        const std::string glob = real.execute(
            "glob_search",
            R"({"pattern":"*.txt","path":"emberforge_test_search_dir"})");
        check(contains(glob, "one.txt") && !contains(glob, "two.log"),
              "glob_search_matches_extension");

        const std::string grep = real.execute(
            "grep_search",
            R"({"pattern":"needle","path":"emberforge_test_search_dir"})");
        check(contains(grep, "one.txt:1:") && contains(grep, "needle here"),
              "grep_search_finds_line");

        std::filesystem::remove_all(dir);
    }

    // ------------------------------------------------------------------
    // Structural tools (web/notebook/agent/skill) are recognized and accepted
    // by the executor (no local backend, but not "unknown tool").
    // ------------------------------------------------------------------
    {
        RealToolExecutor real;
        const std::string web = real.execute("web", R"({"url":"https://example.test"})");
        check(contains(web, "[web] accepted") && !contains(web, "unknown tool"),
              "web_tool_accepted_structural");
    }

    if (g_failures == 0) {
        std::cout << "All tool registry tests PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " tool registry test(s) FAILED\n";
    return 1;
}
