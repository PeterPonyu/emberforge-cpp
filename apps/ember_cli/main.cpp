#include <iostream>

#include "emberforge/emberforge.hpp"

int main() {
    emberforge::system::StarterSystemApplication app;
    const auto demo_outputs = app.run_demo();
    app.shutdown();
    const auto report = app.report();

    std::cout << "emberforge-cpp starter\n";
    std::cout << "system: " << report.app_name << '\n';
    std::cout << "lifecycle: " << report.lifecycle_state << '\n';
    std::cout << "commands: " << report.command_count << '\n';
    std::cout << "tools: " << report.tool_count << '\n';
    std::cout << "plugins: " << report.plugin_count << '\n';
    std::cout << "handled requests: " << report.handled_request_count << '\n';
    std::cout << "server: " << report.server_description << '\n';
    std::cout << "lsp: " << report.lsp_summary << '\n';
    std::cout << "rust anchor: " << report.rust_anchor << '\n';
    std::cout << "turns: " << report.turn_count << '\n';
    for (const auto& output : demo_outputs) {
        std::cout << output << '\n';
    }
    std::cout << "last route: "
              << (report.last_route ? *report.last_route : std::string{"none"}) << '\n';
    std::cout << "last phases: ";
    for (std::size_t index = 0; index < report.last_phase_history.size(); ++index) {
        if (index > 0) {
            std::cout << " -> ";
        }
        std::cout << report.last_phase_history[index];
    }
    std::cout << '\n';
    std::cout << "last turn: "
              << (report.last_turn_input ? *report.last_turn_input : std::string{"none"}) << '\n';
    return 0;
}
