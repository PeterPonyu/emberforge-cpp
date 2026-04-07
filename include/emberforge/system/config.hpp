#pragma once

#include <cstddef>
#include <string>

namespace emberforge::system {

struct StarterSystemConfig {
    std::string app_name{"emberforge-cpp system"};
    int port{8080};
    std::string command_demo_name{"help"};
    std::string greeting{"hello from cpp system"};
    std::string tool_demo_command{"printf translated"};
    std::size_t max_turns{16};
    double max_cost_usd{1.0};
};

} // namespace emberforge::system
