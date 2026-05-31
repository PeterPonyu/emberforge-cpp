#include "emberforge/compat/upstream_paths.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool contains_machine_local_prefix(const std::string& value) {
    return value.find("/home/") != std::string::npos || value.find("file:///home/") != std::string::npos ||
           value.find("C:\\Users\\") != std::string::npos || value.find("C:/Users/") != std::string::npos;
}

} // namespace

int main() {
    const auto paths = emberforge::compat::default_upstream_paths();
    const std::array<std::string, 3> values = {
        paths.claude_commands_ts,
        paths.claude_tools_ts,
        paths.ember_runtime_lib_rs,
    };

    for (const auto& value : values) {
        if (value.empty()) {
            std::cerr << "upstream path default should not be empty\n";
            return EXIT_FAILURE;
        }
        if (contains_machine_local_prefix(value)) {
            std::cerr << "upstream path default leaks a machine-local path: " << value << "\n";
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
