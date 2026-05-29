#include "emberforge/plugins/hook_runner.hpp"

#include "emberforge/plugins/hook_executor.hpp"

#include <string>
#include <utility>

namespace emberforge::plugins {

// ── HookRegistry ───────────────────────────────────────────────────────────

void HookRegistry::add_command(HookEvent event, std::string command) {
    commands[event].push_back(std::move(command));
}

const std::vector<std::string>& HookRegistry::commands_for(HookEvent event) const {
    static const std::vector<std::string> kEmpty;
    const auto it = commands.find(event);
    return it == commands.end() ? kEmpty : it->second;
}

// ── HookRunner ─────────────────────────────────────────────────────────────

HookRunner::HookRunner(HookRegistry registry) : registry_(std::move(registry)) {}

HookRunResult HookRunner::run_pre_tool_use(const std::string& tool_name,
                                           const std::string& tool_input) const {
    return run_commands(HookEvent::PreToolUse, registry_.commands_for(HookEvent::PreToolUse),
                        tool_name, tool_input, /*tool_output=*/"", /*has_output=*/false,
                        /*is_error=*/false);
}

HookRunResult HookRunner::run_post_tool_use(const std::string& tool_name,
                                            const std::string& tool_input,
                                            const std::string& tool_output,
                                            bool is_error) const {
    return run_commands(HookEvent::PostToolUse, registry_.commands_for(HookEvent::PostToolUse),
                        tool_name, tool_input, tool_output, /*has_output=*/true, is_error);
}

void HookRunner::fire_event(HookEvent event) const {
    fire_event_with_context(event, "", "");
}

void HookRunner::fire_event_with_context(HookEvent event,
                                         const std::string& context_key,
                                         const std::string& context_value) const {
    const auto& commands = registry_.commands_for(event);
    if (commands.empty()) {
        return;
    }
    // Fire-and-forget: lifecycle events do not block on or honor a deny.
    (void)run_commands(event, commands, context_key, context_value, /*tool_output=*/"",
                       /*has_output=*/false, /*is_error=*/false);
}

HookRunResult HookRunner::run_commands(HookEvent event,
                                       const std::vector<std::string>& commands,
                                       const std::string& tool_name,
                                       const std::string& tool_input,
                                       const std::string& tool_output,
                                       bool has_output,
                                       bool is_error) const {
    if (commands.empty()) {
        return HookRunResult::allow({});
    }

    HookContext ctx;
    ctx.event = event;
    ctx.tool_name = tool_name;
    ctx.tool_input = tool_input;
    ctx.tool_output = tool_output;
    ctx.has_output = has_output;
    ctx.is_error = is_error;

    std::vector<std::string> messages;
    for (const std::string& command : commands) {
        const CommandHookExecutor executor{CommandBackend{command}};
        const HookCommandOutcome outcome = executor.run(ctx);
        switch (outcome.decision) {
            case HookDecision::Allow:
                if (!outcome.message.empty()) {
                    messages.push_back(outcome.message);
                }
                break;
            case HookDecision::Deny: {
                std::string msg = outcome.message.empty()
                                      ? (std::string(to_wire_name(event)) +
                                         " hook denied tool `" + tool_name + "`")
                                      : outcome.message;
                messages.push_back(std::move(msg));
                return HookRunResult{true, std::move(messages)};
            }
            case HookDecision::Warn:
                messages.push_back(outcome.message);
                break;
        }
    }
    return HookRunResult::allow(std::move(messages));
}

} // namespace emberforge::plugins
