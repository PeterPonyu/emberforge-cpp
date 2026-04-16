#include <iostream>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "emberforge/emberforge.hpp"
#include "emberforge/api/ollama_provider.hpp"
#include "emberforge/commands/registry.hpp"
#include "emberforge/system/doctor.hpp"
#include "emberforge/ui/command_dispatch.hpp"
#include "emberforge/ui/repl.hpp"

static bool wants_repl(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--repl") == 0) {
            return true;
        }
    }
    // Default to REPL when no arguments given and stdin is a TTY.
    return (argc == 1) && (isatty(STDIN_FILENO) != 0);
}

static std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : s) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

static std::string join_argv(int argc, char* argv[], int start_index) {
    std::string combined;
    for (int i = start_index; i < argc; ++i) {
        if (i > start_index) combined.push_back(' ');
        combined += argv[i];
    }
    return combined;
}

static std::string resolve_cli_model_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            return argv[i + 1];
        }
        if (arg.rfind("--model=", 0) == 0) {
            return arg.substr(std::string{"--model="}.size());
        }
    }
    return {};
}

static int first_non_flag_arg_index(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model") {
            ++i;
            continue;
        }
        if (arg.rfind("--model=", 0) == 0) {
            continue;
        }
        return i;
    }
    return -1;
}

int main(int argc, char* argv[]) {
    try {
        const char* env_base_url = std::getenv("OLLAMA_BASE_URL");
        const char* env_model    = std::getenv("EMBER_MODEL");
        const std::string base_url = env_base_url ? env_base_url : "http://localhost:11434";
        const std::string cli_model = resolve_cli_model_arg(argc, argv);
        const std::string model    = !cli_model.empty() ? cli_model : (env_model ? env_model : "qwen3:8b");
        if (!cli_model.empty()) {
            setenv("EMBER_MODEL", cli_model.c_str(), 1);
        }

        auto provider = std::make_unique<emberforge::api::OllamaProvider>(base_url, model);
        emberforge::system::StarterSystemApplication app(std::move(provider));

        const int command_arg_index = first_non_flag_arg_index(argc, argv);

        if (command_arg_index != -1 && std::strcmp(argv[command_arg_index], "doctor") == 0) {
            const auto report = app.report();
            if (command_arg_index + 1 < argc && std::strcmp(argv[command_arg_index + 1], "status") == 0) {
                std::cout << "emberforge-cpp doctor status\n";
                std::cout << "lifecycle: " << report.lifecycle_state << '\n';
                std::cout << "handled_requests: " << report.handled_request_count << '\n';
                std::cout << "turns: " << report.turn_count << '\n';
                std::cout << "last_route: "
                          << (report.last_route ? *report.last_route : std::string{"none"}) << '\n';
            } else {
                std::cout << emberforge::system::build_doctor_report(
                    report,
                    base_url,
                    model,
                    std::getenv("ANTHROPIC_API_KEY") != nullptr,
                    std::getenv("XAI_API_KEY") != nullptr
                ) << '\n';
            }
            app.shutdown();
            return 0;
        }

        if (command_arg_index != -1 && argv[command_arg_index][0] == '/') {
            emberforge::ui::CommandDispatch dispatch;
            const std::string raw_command = join_argv(argc, argv, command_arg_index);
            const std::string body = raw_command.substr(1);
            auto parts = split_args(body);
            if (parts.empty()) {
                std::cerr << "empty command\n";
                app.shutdown();
                return 1;
            }
            const std::string cmd = parts[0];
            parts.erase(parts.begin());

            if (cmd == "help") {
                std::cout << "available commands:\n";
                for (const auto& command : emberforge::commands::get_commands()) {
                    std::cout << "  /" << command.name;
                    if (!command.argument_hint.empty()) {
                        std::cout << ' ' << command.argument_hint;
                    }
                    std::cout << " — " << command.description << '\n';
                }
                app.shutdown();
                return 0;
            }

            const int rc = dispatch.invoke(cmd, app, parts);
            app.shutdown();
            return rc == 255 ? 0 : rc;
        }

        if (wants_repl(argc, argv)) {
            emberforge::ui::Repl repl(app);
            return repl.run();
        }

        // --- Demo mode (original behaviour) ---
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
    } catch (const std::exception& ex) {
        std::cerr << "Ollama error: " << ex.what() << '\n';
        return 1;
    }
}
