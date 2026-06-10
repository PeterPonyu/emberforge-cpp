#include "emberforge/commands/registry.hpp"

namespace emberforge::commands {

std::vector<CommandSpec> get_commands() {
    return {
        {"help", "Show available slash commands", CommandCategory::Core, ""},
        {"status", "Report runtime status (model, provider, session)", CommandCategory::Core, ""},
        {"doctor", "Run environment diagnostics (provider connectivity, config)", CommandCategory::Core, "[quick|status]"},
        {"model", "Switch or inspect the active model", CommandCategory::Core, "[model|list]"},
        {"questions", "Inspect and answer task-linked questions", CommandCategory::Session, "[ask <task-id> <text>|pending|answer <question-id> <text>]"},
        {"tasks", "Create and inspect background tasks", CommandCategory::Automation, "[create prompt <text>|list|show <task-id>|stop <task-id>]"},
        {"buddy", "Manage the companion buddy", CommandCategory::Core, "[hatch|rehatch|pet|mute|unmute]"},
        {"compact", "Summarize the current conversation state", CommandCategory::Core, ""},
        {"review", "Review the current workspace changes", CommandCategory::Git, "[scope]"},
        {"commit", "Prepare a commit summary from staged changes", CommandCategory::Git, ""},
        {"pr", "Prepare a pull request summary from the current branch", CommandCategory::Git, "[context]"},
    };
}

} // namespace emberforge::commands
