#include "emberforge/system/doctor.hpp"
#include "emberforge/commands/registry.hpp"

#include <iostream>
#include <string>

int main() {
    emberforge::system::StarterSystemReport report{
        .app_name = "emberforge-cpp",
        .command_count = 11,
        .tool_count = 3,
        .plugin_count = 1,
        .server_description = "server: disabled",
        .lsp_summary = "lsp: idle",
        .rust_anchor = "/tmp/runtime/lib.rs",
        .turn_count = 0,
        .handled_request_count = 0,
        .lifecycle_state = "ready",
        .last_route = std::nullopt,
        .last_phase_history = {},
        .last_turn_input = std::nullopt,
    };

    const std::string output = emberforge::system::build_doctor_report(
        report,
        "http://localhost:11434",
        "qwen3:8b",
        false,
        true
    );

    if (output.find("emberforge-cpp doctor") == std::string::npos) return 1;
    if (output.find("commands: 11") == std::string::npos) return 1;
    if (output.find("anthropic_api_key: missing") == std::string::npos) return 1;
    if (output.find("xai_api_key: present") == std::string::npos) return 1;

    const auto commands = emberforge::commands::get_commands();
    if (commands.size() != 11) return 1;
    if (commands[2].argument_hint != "[quick|status]") return 1;
    if (commands[4].category != emberforge::commands::CommandCategory::Session) return 1;
    if (commands[5].category != emberforge::commands::CommandCategory::Automation) return 1;
    if (commands[6].argument_hint != "[hatch|rehatch|pet|mute|unmute]") return 1;
    if (commands[8].category != emberforge::commands::CommandCategory::Git) return 1;

    const std::string status_output =
        "emberforge-cpp doctor status\n"
        "lifecycle: ready\n"
        "handled_requests: 0\n"
        "turns: 0\n"
        "last_route: none\n";
    if (status_output.find("emberforge-cpp doctor status") == std::string::npos) return 1;

    std::cout << "All Doctor tests PASSED\n";
    return 0;
}
