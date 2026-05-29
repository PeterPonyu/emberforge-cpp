#pragma once

#include <map>
#include <string>
#include <vector>

#include "emberforge/plugins/hook.hpp"
#include "emberforge/plugins/hook_event.hpp"

namespace emberforge::plugins {

// Aggregated hook commands keyed by event — the shape produced by collecting
// the `hooks` blocks from enabled plugin manifests (CROSS_PORT_CONTRACT.md §5).
// Values are shell command strings, matching the Rust PluginHooks model.
struct HookRegistry {
    std::map<HookEvent, std::vector<std::string>> commands;

    void add_command(HookEvent event, std::string command);
    [[nodiscard]] const std::vector<std::string>& commands_for(HookEvent event) const;
    [[nodiscard]] bool empty() const { return commands.empty(); }
};

// Drives hook execution and lifecycle dispatch. Mirrors the Rust HookRunner:
//   * run_pre_tool_use / run_post_tool_use return an aggregate decision that
//     short-circuits on the first deny;
//   * fire_event / fire_event_with_context are fire-and-forget lifecycle
//     dispatchers (no tool context, result discarded).
//
// This is the WIP dispatcher skeleton: it owns the registry and the
// match-rule/exit-code plumbing. Full wiring into the live tool-execution
// pipeline (real_executor / control_sequence) is a remaining checklist item.
class HookRunner {
public:
    HookRunner() = default;
    explicit HookRunner(HookRegistry registry);

    // Tool-event entry points. tool_input is the raw JSON string.
    [[nodiscard]] HookRunResult run_pre_tool_use(const std::string& tool_name,
                                                 const std::string& tool_input) const;
    [[nodiscard]] HookRunResult run_post_tool_use(const std::string& tool_name,
                                                  const std::string& tool_input,
                                                  const std::string& tool_output,
                                                  bool is_error) const;

    // Lifecycle event dispatch (no tool context). Fire-and-forget.
    void fire_event(HookEvent event) const;
    void fire_event_with_context(HookEvent event,
                                 const std::string& context_key,
                                 const std::string& context_value) const;

    [[nodiscard]] const HookRegistry& registry() const { return registry_; }

private:
    // Run every command registered for `event`, aggregating messages and
    // short-circuiting on the first deny. `match_rule` is applied to tool
    // events when provided.
    [[nodiscard]] HookRunResult run_commands(HookEvent event,
                                             const std::vector<std::string>& commands,
                                             const std::string& tool_name,
                                             const std::string& tool_input,
                                             const std::string& tool_output,
                                             bool has_output,
                                             bool is_error) const;

    HookRegistry registry_;
};

} // namespace emberforge::plugins
