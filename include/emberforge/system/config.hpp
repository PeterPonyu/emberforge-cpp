#pragma once

#include <string>

namespace emberforge::system {

struct StarterSystemConfig {
    std::string app_name{"emberforge-cpp system"};
    int port{8080};
    std::string command_demo_name{"help"};
    std::string greeting{"hello from cpp system"};
    std::string tool_demo_command{"printf translated"};
};

} // namespace emberforge::system
