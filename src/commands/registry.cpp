#include "emberforge/commands/registry.hpp"

namespace emberforge::commands {

std::vector<CommandSpec> get_commands() {
    return {
        {"help", "Show the starter command registry", CommandCategory::Core, ""},
        {"status", "Report the translated architecture status", CommandCategory::Core, ""},
        {"doctor", "Run translated environment diagnostics", CommandCategory::Core, "[quick|status]"},
        {"model", "Switch or inspect the active model", CommandCategory::Core, "[model|list]"},
        {"questions", "Inspect and answer task-linked questions", CommandCategory::Session, "[ask <task-id> <text>|pending|answer <question-id> <text>]"},
        {"tasks", "Create and inspect translated background tasks", CommandCategory::Automation, "[create prompt <text>|list|show <task-id>|stop <task-id>]"},
        {"buddy", "Manage the translated companion buddy", CommandCategory::Core, "[hatch|rehatch|pet|mute|unmute]"},
        {"compact", "Summarize the current conversation state", CommandCategory::Core, ""},
        {"review", "Review the current workspace changes", CommandCategory::Git, "[scope]"},
        {"commit", "Prepare a translated commit summary", CommandCategory::Git, ""},
        {"pr", "Prepare a translated pull request summary", CommandCategory::Git, "[context]"},
    };
}

} // namespace emberforge::commands
