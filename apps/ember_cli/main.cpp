#include <iostream>
#include <cstdlib>
#include <memory>
#include <string>

#include "emberforge/emberforge.hpp"
#include "emberforge/api/ollama_provider.hpp"

int main() {
    const char* env_base_url = std::getenv("OLLAMA_BASE_URL");
    const char* env_model    = std::getenv("EMBER_MODEL");
    const std::string base_url = env_base_url ? env_base_url : "http://localhost:11434";
    const std::string model    = env_model    ? env_model    : "qwen3:8b";

    auto provider = std::make_unique<emberforge::api::OllamaProvider>(base_url, model);
    emberforge::system::StarterSystemApplication app(std::move(provider));
    const auto demo_outputs = app.run_demo();
    app.shutdown();
    const auto report = app.report();

    std::cout << "emberforge-cpp starter\n";
    std::cout << "provider: ollama @ " << base_url << " model=" << model << '\n';
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
    const char* sep = "";
    for (const auto& phase : report.last_phase_history) {
        std::cout << sep << phase;
        sep = " -> ";
    }
    std::cout << '\n';
    std::cout << "last turn: "
              << (report.last_turn_input ? *report.last_turn_input : std::string{"none"}) << '\n';
    return 0;
}
