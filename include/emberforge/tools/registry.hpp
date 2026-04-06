#pragma once

#include <string>
#include <vector>

#include "emberforge/tools/spec.hpp"

namespace emberforge::tools {

class ToolExecutor {
public:
    virtual ~ToolExecutor() = default;
    virtual std::string execute(const std::string& tool_name, const std::string& input) = 0;
};

class MockToolExecutor final : public ToolExecutor {
public:
    std::string execute(const std::string& tool_name, const std::string& input) override;
};

std::vector<ToolSpec> get_tools();

} // namespace emberforge::tools
