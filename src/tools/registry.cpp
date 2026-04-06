#include "emberforge/tools/registry.hpp"

namespace emberforge::tools {

std::vector<ToolSpec> get_tools() {
    return {
        {"read_file", "Read workspace files"},
        {"grep_search", "Search text across files"},
        {"bash", "Run shell commands"},
    };
}

std::string MockToolExecutor::execute(const std::string& tool_name, const std::string& input) {
    return "[cpp tool] " + tool_name + " => " + input;
}

} // namespace emberforge::tools
