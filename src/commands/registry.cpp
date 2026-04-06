#include "emberforge/commands/registry.hpp"

namespace emberforge::commands {

std::vector<CommandSpec> get_commands() {
    return {
        {"help", "Show the starter command registry"},
        {"status", "Report the translated architecture status"},
        {"model", "Demonstrate a Rust-style command entry"},
    };
}

} // namespace emberforge::commands
