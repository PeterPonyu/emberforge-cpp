#pragma once

#include <string>

namespace emberforge::commands {

enum class CommandCategory {
    Core,
    Workspace,
    Session,
    Git,
    Automation,
};

struct CommandSpec {
    std::string name;
    std::string description;
    CommandCategory category;
    std::string argument_hint;
};

} // namespace emberforge::commands
