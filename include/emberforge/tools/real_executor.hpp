#pragma once

#include "emberforge/tools/registry.hpp"
#include <string>

namespace emberforge::tools {

class RealToolExecutor final : public ToolExecutor {
public:
    std::string execute(const std::string& tool_name, const std::string& input) override;

private:
    // Returns true if `path` resolves to a location inside the current
    // workspace (cwd). Used by read_file and write_file to refuse paths
    // that escape the workspace (absolute or traversal-style).
    static bool is_within_workspace(const std::string& path);

    std::string read_file(const std::string& path);
    std::string write_file(const std::string& path, const std::string& content);
    std::string edit_file(const std::string& path, const std::string& old_string,
                          const std::string& new_string, bool replace_all);
    std::string glob_search(const std::string& pattern, const std::string& root);
    std::string grep_search(const std::string& pattern, const std::string& root);
    std::string bash(const std::string& command);
};

} // namespace emberforge::tools
