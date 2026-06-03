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

// Guard against re-introducing another tool's brand or the reference
// implementation's language/crate layout into these compat constants.
bool contains_brand_token(const std::string& value) {
    static const std::array<std::string, 6> tokens = {"claude", "crates/", "cargo", ".rs", ".ts", "code-src"};
    for (const auto& token : tokens) {
        if (value.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    const auto paths = emberforge::compat::default_upstream_paths();
    const std::array<std::string, 3> values = {
        paths.upstream_commands_ref,
        paths.upstream_tools_ref,
        paths.upstream_runtime_ref,
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
        if (contains_brand_token(value)) {
            std::cerr << "upstream path default leaks a brand/language token: " << value << "\n";
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
