// test_hook_engine.cpp
//
// Unit tests for the Hook & Lifecycle execution engine (issue #10).
// Covers: the 17 HookEvent variants + wire names, match-rule glob grammar,
// command-backend exit-code semantics (0 allow / 2 deny / other warn) via a
// real subprocess, the payload builder, and the lifecycle dispatcher.
// No external test framework — plain checks and return 0/1, matching the
// repo's other test executables.

#include "emberforge/plugins/hook.hpp"
#include "emberforge/plugins/hook_event.hpp"
#include "emberforge/plugins/hook_executor.hpp"
#include "emberforge/plugins/hook_runner.hpp"

#include <iostream>
#include <string>

namespace ef = emberforge::plugins;

static int failures = 0;

static void check(bool cond, const std::string& label) {
    if (cond) {
        std::cout << "PASS (" << label << ")\n";
    } else {
        std::cerr << "FAIL (" << label << ")\n";
        ++failures;
    }
}

int main() {
    // ------------------------------------------------------------------
    // 1. All 17 HookEvent variants exist and round-trip through wire names.
    // ------------------------------------------------------------------
    check(ef::kAllHookEvents.size() == 17, "hook_event_count_is_17");

    {
        bool round_trip_ok = true;
        for (const ef::HookEvent event : ef::kAllHookEvents) {
            const auto name = ef::to_wire_name(event);
            const auto parsed = ef::hook_event_from_wire_name(name);
            if (!parsed || *parsed != event) {
                round_trip_ok = false;
            }
        }
        check(round_trip_ok, "hook_event_wire_name_round_trip");
    }

    check(ef::to_wire_name(ef::HookEvent::PreToolUse) == "PreToolUse",
          "pretooluse_wire_name");
    check(ef::to_wire_name(ef::HookEvent::FileChanged) == "FileChanged",
          "filechanged_wire_name");
    check(!ef::hook_event_from_wire_name("NotAnEvent").has_value(),
          "unknown_wire_name_rejected");
    check(ef::is_tool_event(ef::HookEvent::PreToolUse) &&
              ef::is_tool_event(ef::HookEvent::ToolError) &&
              !ef::is_tool_event(ef::HookEvent::SessionStart),
          "is_tool_event_classification");

    // ------------------------------------------------------------------
    // 2. Glob match grammar.
    // ------------------------------------------------------------------
    check(ef::glob_match("rm *", "sudo rm -rf /"), "glob_trailing_star_substring");
    check(ef::glob_match("git push*", "run git push origin"), "glob_prefix_match");
    check(!ef::glob_match("rm *", "echo safe"), "glob_no_match");
    check(ef::glob_match("LS", "do an ls thing"), "glob_case_insensitive");
    check(ef::glob_match("*", "anything at all"), "glob_bare_star_matches_all");

    // ------------------------------------------------------------------
    // 3. Match rule filtering.
    // ------------------------------------------------------------------
    {
        ef::HookMatchRule rule;
        rule.tool_names = {"bash"};
        rule.commands = {"rm *"};
        check(rule.matches("bash", "rm -rf build"), "match_rule_tool_and_command");
        check(!rule.matches("read_file", "rm -rf build"), "match_rule_wrong_tool");
        check(!rule.matches("bash", "echo hi"), "match_rule_command_miss");

        ef::HookMatchRule match_all;  // empty rule matches everything
        check(match_all.matches("anything", "any input"), "match_rule_empty_matches_all");
    }

    // ------------------------------------------------------------------
    // 4. Exit-code -> decision mapping (pure function).
    // ------------------------------------------------------------------
    check(ef::CommandHookExecutor::decision_for_exit_code(0) == ef::HookDecision::Allow,
          "exit0_allows");
    check(ef::CommandHookExecutor::decision_for_exit_code(2) == ef::HookDecision::Deny,
          "exit2_denies");
    check(ef::CommandHookExecutor::decision_for_exit_code(1) == ef::HookDecision::Warn,
          "exit1_warns");
    check(ef::CommandHookExecutor::decision_for_exit_code(42) == ef::HookDecision::Warn,
          "exit_other_warns");

    // ------------------------------------------------------------------
    // 5. HTTP status -> decision mapping.
    // ------------------------------------------------------------------
    check(ef::HttpHookExecutor::decision_for_status(200) == ef::HookDecision::Allow,
          "http_2xx_allows");
    check(ef::HttpHookExecutor::decision_for_status(403) == ef::HookDecision::Deny,
          "http_403_denies");
    check(ef::HttpHookExecutor::decision_for_status(500) == ef::HookDecision::Warn,
          "http_5xx_warns");

    // ------------------------------------------------------------------
    // 6. Payload builder embeds wire name + raw/parsed input.
    // ------------------------------------------------------------------
    {
        ef::HookContext ctx;
        ctx.event = ef::HookEvent::PreToolUse;
        ctx.tool_name = "bash";
        ctx.tool_input = R"({"command":"ls /tmp"})";
        const std::string payload = ef::build_hook_payload(ctx);
        check(payload.find("\"hook_event_name\":\"PreToolUse\"") != std::string::npos,
              "payload_has_event_name");
        check(payload.find("\"tool_name\":\"bash\"") != std::string::npos,
              "payload_has_tool_name");
        check(payload.find("ls /tmp") != std::string::npos, "payload_has_input");
        check(payload.find("\"tool_result_is_error\":false") != std::string::npos,
              "payload_has_is_error");
    }

    // ------------------------------------------------------------------
    // 7. Command backend end-to-end via a real subprocess.
    // ------------------------------------------------------------------
    {
        // exit 0 with stdout -> Allow + captured message.
        ef::CommandHookExecutor allow_exec{ef::CommandBackend{"printf 'looks good'; exit 0"}};
        ef::HookContext ctx;
        ctx.event = ef::HookEvent::PreToolUse;
        ctx.tool_name = "bash";
        ctx.tool_input = R"({"command":"pwd"})";
        const auto allow_out = allow_exec.run(ctx);
        check(allow_out.decision == ef::HookDecision::Allow && allow_out.message == "looks good",
              "command_exit0_allow_captures_stdout");

        // exit 2 -> Deny.
        ef::CommandHookExecutor deny_exec{ef::CommandBackend{"printf 'blocked'; exit 2"}};
        const auto deny_out = deny_exec.run(ctx);
        check(deny_out.decision == ef::HookDecision::Deny && deny_out.message == "blocked",
              "command_exit2_deny");

        // exit 1 -> Warn (and tool execution still allowed).
        ef::CommandHookExecutor warn_exec{ef::CommandBackend{"exit 1"}};
        const auto warn_out = warn_exec.run(ctx);
        check(warn_out.decision == ef::HookDecision::Warn, "command_exit1_warn");
    }

    // ------------------------------------------------------------------
    // 8. HookRunner aggregation + deny short-circuit.
    // ------------------------------------------------------------------
    {
        ef::HookRegistry registry;
        registry.add_command(ef::HookEvent::PreToolUse, "printf 'first'; exit 0");
        registry.add_command(ef::HookEvent::PreToolUse, "printf 'denied here'; exit 2");
        registry.add_command(ef::HookEvent::PreToolUse, "printf 'never runs message'; exit 0");
        ef::HookRunner runner{std::move(registry)};

        const auto result = runner.run_pre_tool_use("bash", R"({"command":"pwd"})");
        check(result.is_denied(), "runner_denies_on_exit2");
        // first allow message + deny message captured; third command does run
        // after the deny short-circuit only if it precedes it — here it does not.
        check(result.messages.size() == 2 && result.messages.front() == "first" &&
                  result.messages.back() == "denied here",
              "runner_aggregates_until_deny");
    }

    {
        // No hooks registered -> allow with no messages.
        ef::HookRunner empty_runner;
        const auto result = empty_runner.run_post_tool_use("read_file", "{}", "ok", false);
        check(!result.is_denied() && result.messages.empty(), "runner_empty_allows");
    }

    // ------------------------------------------------------------------
    // 9. Lifecycle dispatch is fire-and-forget (no throw, no deny surfaced).
    // ------------------------------------------------------------------
    {
        ef::HookRegistry registry;
        registry.add_command(ef::HookEvent::SessionStart, "exit 0");
        ef::HookRunner runner{std::move(registry)};
        runner.fire_event(ef::HookEvent::SessionStart);
        runner.fire_event_with_context(ef::HookEvent::CwdChanged, "new_cwd", "/tmp");
        check(true, "lifecycle_fire_event_does_not_throw");
    }

    if (failures == 0) {
        std::cout << "All Hook engine tests PASSED\n";
        return 0;
    }
    std::cerr << failures << " Hook engine test(s) FAILED\n";
    return 1;
}
