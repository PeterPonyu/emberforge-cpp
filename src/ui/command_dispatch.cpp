#include "emberforge/ui/command_dispatch.hpp"

#include <iostream>

#include "emberforge/system/application.hpp"

namespace emberforge::ui {

CommandDispatch::CommandDispatch() {
    // /help — list all registered commands
    register_handler("help", [](emberforge::system::StarterSystemApplication& /*app*/,
                                const std::vector<std::string>& /*args*/) -> int {
        return 0;
    });

    // /status — short summary line
    register_handler("status", [](emberforge::system::StarterSystemApplication& app,
                                  const std::vector<std::string>& /*args*/) -> int {
        const auto report = app.report();
        std::cout << "emberforge C++ — app: " << report.app_name
                  << " — lifecycle: " << report.lifecycle_state
                  << " — turns: " << report.turn_count
                  << '\n';
        return 0;
    });

    // /model — print the currently configured model
    register_handler("model", [](emberforge::system::StarterSystemApplication& /*app*/,
                                 const std::vector<std::string>& /*args*/) -> int {
        const char* env_model = std::getenv("EMBER_MODEL");
        const std::string model = env_model ? env_model : "qwen3:8b";
        std::cout << "model: " << model << '\n';
        return 0;
    });

    // /clear — ANSI clear screen
    register_handler("clear", [](emberforge::system::StarterSystemApplication& /*app*/,
                                 const std::vector<std::string>& /*args*/) -> int {
        std::cout << "\x1b[2J\x1b[H" << std::flush;
        return 0;
    });

    // /quit — signal the REPL to stop
    register_handler("quit", [](emberforge::system::StarterSystemApplication& /*app*/,
                                const std::vector<std::string>& /*args*/) -> int {
        return 255;
    });
}

void CommandDispatch::register_handler(std::string name, CommandHandler handler) {
    handlers_[std::move(name)] = std::move(handler);
}

int CommandDispatch::invoke(std::string name,
                            emberforge::system::StarterSystemApplication& app,
                            std::vector<std::string> args) {
    auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        std::cerr << "unknown command: /" << name << "  (try /help)\n";
        return 1;
    }
    return it->second(app, args);
}

} // namespace emberforge::ui
