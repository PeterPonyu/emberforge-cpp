#pragma once

#include "emberforge/tools/registry.hpp"
#include <string>

namespace emberforge::tools {

class RealToolExecutor final : public ToolExecutor {
public:
    std::string execute(const std::string& tool_name, const std::string& input) override;

private:
    std::string read_file(const std::string& path);
    std::string write_file(const std::string& path, const std::string& content);
    std::string bash(const std::string& command);
};

} // namespace emberforge::tools
