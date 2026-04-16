#include "emberforge/tools/registry.hpp"

namespace emberforge::tools {

std::vector<ToolSpec> get_tools() {
    return {
        {"read_file", "Read workspace files"},
        {"grep_search", "Search text across files"},
        {"bash", "Run shell commands"},
        {"ask_user_question", "Create a task-linked clarification request"},
        {"task_create", "Create a tracked task record"},
        {"task_get", "Read a tracked task record"},
        {"task_list", "List tracked task records"},
        {"task_stop", "Stop a tracked task record"},
    };
}

std::string MockToolExecutor::execute(const std::string& tool_name, const std::string& input) {
    return "[cpp tool] " + tool_name + " => " + input;
}

} // namespace emberforge::tools
