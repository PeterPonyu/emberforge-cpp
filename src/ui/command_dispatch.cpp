#include "emberforge/ui/command_dispatch.hpp"

#include <algorithm>
#include <exception>
#include <iostream>

#include "emberforge/api/ollama_provider.hpp"
#include "emberforge/runtime/model_router.hpp"
#include "emberforge/system/application.hpp"
#include "emberforge/system/buddy.hpp"
#include "emberforge/commands/registry.hpp"
#include "emberforge/system/doctor.hpp"

namespace emberforge::ui {

namespace {

// Render the "Available models" report for the app's active provider. When the
// provider is an OllamaProvider, query the live local-tag list via /api/tags and
// fold in the current model; otherwise (hosted) report no local models. Network
// failures degrade to an "unreachable" status rather than throwing — parity with
// the Rust discover_available_models.
std::string render_models_report(emberforge::system::StarterSystemApplication& app) {
    const std::string current_model = app.provider().current_model();

    std::vector<std::string> models;
    std::string status;
    if (auto* ollama = dynamic_cast<emberforge::api::OllamaProvider*>(&app.provider())) {
        if (!current_model.empty()) {
            models.push_back(current_model);
        }
        try {
            for (const auto& tag : ollama->list_models()) {
                if (std::find(models.begin(), models.end(), tag) == models.end()) {
                    models.push_back(tag);
                }
            }
            std::sort(models.begin(), models.end());
            status = models.empty()
                         ? "reachable, but no local models were reported"
                         : "reachable - " + std::to_string(models.size()) +
                               " local model(s) detected";
        } catch (const std::exception& ex) {
            models.clear();
            if (!current_model.empty()) {
                models.push_back(current_model);
            }
            status = "unreachable - showing the current session model only (" +
                     std::string(ex.what()) + ")";
        }
    } else {
        status = "not applicable (active provider is not Ollama)";
    }

    return emberforge::runtime::render_available_models_report(current_model, status, models);
}

void print_help_lines() {
    std::cout << "available commands:\n";
    for (const auto& command : emberforge::commands::get_commands()) {
        std::cout << "  /" << command.name;
        if (!command.argument_hint.empty()) {
            std::cout << ' ' << command.argument_hint;
        }
        std::cout << " — " << command.description << '\n';
    }
}

std::string render_task(const emberforge::system::StarterTaskRecord& task,
                        const std::string& header = "[command] tasks show") {
    return header + "\n" +
           "task_id: " + task.id + "\n" +
           "kind: " + task.kind + "\n" +
           "status: " + emberforge::system::to_string(task.status) + "\n" +
           "input: " + task.input + "\n" +
           "question_id: " + (task.question_id ? *task.question_id : std::string{"none"}) + "\n" +
           "answer: " + (task.answer ? *task.answer : std::string{"none"}) + "\n" +
           "output: " + (task.output ? *task.output : std::string{"none"});
}

} // namespace

CommandDispatch::CommandDispatch() {
    // /help — list all registered commands
    register_handler("help", [](emberforge::system::StarterSystemApplication& /*app*/,
                                const std::vector<std::string>& /*args*/) -> int {
        print_help_lines();
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

    // /doctor — translated placeholder for local environment diagnostics
    register_handler("doctor", [](emberforge::system::StarterSystemApplication& app,
                                  const std::vector<std::string>& args) -> int {
        const auto report = app.report();
        if (!args.empty() && args[0] == "status") {
            std::cout << "emberforge-cpp doctor status\n";
            std::cout << "lifecycle: " << report.lifecycle_state << '\n';
            std::cout << "handled_requests: " << report.handled_request_count << '\n';
            std::cout << "turns: " << report.turn_count << '\n';
            std::cout << "last_route: "
                      << (report.last_route ? *report.last_route : std::string{"none"}) << '\n';
            return 0;
        }

        const char* env_base_url = std::getenv("OLLAMA_BASE_URL");
        const char* env_model    = std::getenv("EMBER_MODEL");
        const std::string base_url = env_base_url ? env_base_url : "http://localhost:11434";
        const std::string model    = env_model    ? env_model    : "qwen3:8b";
        std::cout << emberforge::system::build_doctor_report(
            report,
            base_url,
            model,
            std::getenv("ANTHROPIC_API_KEY") != nullptr,
            std::getenv("XAI_API_KEY") != nullptr
        ) << '\n';
        return 0;
    });

    // /model [list|auto|hybrid|<name>]
    //   - no args / "show": print the active model
    //   - "list": render the live "Available models" report (queries /api/tags)
    //   - "auto" / "hybrid": resolve the routing strategy's default model pair
    //     and switch the active model to the strategy's primary (fast/local)
    //   - "<name>": switch the active model for subsequent turns
    // Mirrors the Rust `/model` handling (ember-cli main.rs:1532-1605).
    register_handler("model", [](emberforge::system::StarterSystemApplication& app,
                                 const std::vector<std::string>& args) -> int {
        if (args.empty() || args[0] == "show") {
            const std::string current = app.provider().current_model();
            std::cout << "model: " << (current.empty() ? std::string{"(none)"} : current) << '\n';
            return 0;
        }
        if (args[0] == "list") {
            std::cout << render_models_report(app) << '\n';
            return 0;
        }

        // auto / hybrid resolve to a routing strategy; switch to its primary
        // model so subsequent turns use it. A bare name is a Fixed strategy.
        const emberforge::runtime::RoutingStrategy strategy =
            emberforge::runtime::parse_strategy(args[0]);
        const std::string selected = strategy.primary;
        app.provider().set_model(selected);
        setenv("EMBER_MODEL", selected.c_str(), 1);
        if (strategy.kind == emberforge::runtime::RoutingStrategyKind::Auto) {
            std::cout << "model: auto (fast=" << strategy.primary
                      << ", capable=" << strategy.secondary
                      << ") — active model set to " << selected << '\n';
        } else if (strategy.kind == emberforge::runtime::RoutingStrategyKind::Hybrid) {
            std::cout << "model: hybrid (local=" << strategy.primary
                      << ", cloud=" << strategy.secondary
                      << ") — active model set to " << selected << '\n';
        } else {
            std::cout << "model: " << selected << '\n';
        }
        return 0;
    });

    register_handler("buddy", [](emberforge::system::StarterSystemApplication& app,
                                 const std::vector<std::string>& args) -> int {
        std::string payload;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                payload.push_back(' ');
            }
            payload += args[i];
        }
        std::cout << emberforge::system::execute_buddy_command(app.buddy(), payload) << '\n';
        return 0;
    });

    register_handler("tasks", [](emberforge::system::StarterSystemApplication& app,
                                 const std::vector<std::string>& args) -> int {
        const auto action = args.empty() ? std::string{"list"} : args[0];
        if (action == "create") {
            if (args.size() < 3 || args[1] != "prompt") {
                std::cout << "[command] tasks: usage /tasks create prompt <text>\n";
                return 0;
            }
            std::string input;
            for (std::size_t i = 2; i < args.size(); ++i) {
                if (i > 2) input.push_back(' ');
                input += args[i];
            }
            std::cout << render_task(app.task_question_store().create_prompt_task(input), "[command] tasks create") << '\n';
            return 0;
        }
        if (action == "list") {
            const auto tasks = app.task_question_store().list_tasks();
            std::cout << "[command] tasks list\n";
            std::cout << "tasks: " << tasks.size() << '\n';
            for (const auto& task : tasks) {
                std::cout << task.id << " :: " << emberforge::system::to_string(task.status) << " :: " << task.input << '\n';
            }
            return 0;
        }
        if (action == "show") {
            if (args.size() < 2) {
                std::cout << "[command] tasks: usage /tasks show <task-id>\n";
                return 0;
            }
            const auto task = app.task_question_store().get_task(args[1]);
            if (!task) {
                std::cout << "[command] tasks: task not found " << args[1] << '\n';
                return 0;
            }
            std::cout << render_task(*task) << '\n';
            return 0;
        }
        if (action == "stop") {
            if (args.size() < 2) {
                std::cout << "[command] tasks: usage /tasks stop <task-id>\n";
                return 0;
            }
            try {
                std::cout << render_task(app.task_question_store().stop_task(args[1]), "[command] tasks stop") << '\n';
            } catch (const std::exception& ex) {
                std::cout << "[command] tasks: " << ex.what() << '\n';
            }
            return 0;
        }
        std::cout << "[command] tasks: unsupported action " << action << '\n';
        return 0;
    });

    register_handler("questions", [](emberforge::system::StarterSystemApplication& app,
                                     const std::vector<std::string>& args) -> int {
        const auto action = args.empty() ? std::string{"pending"} : args[0];
        if (action == "pending") {
            const auto questions = app.task_question_store().list_questions(emberforge::system::StarterQuestionStatus::Pending);
            std::cout << "[command] questions pending\n";
            std::cout << "pending: " << questions.size() << '\n';
            for (const auto& question : questions) {
                std::cout << question.id << " -> " << question.task_id << " :: " << question.text << '\n';
            }
            return 0;
        }
        if (action == "ask") {
            if (args.size() < 3) {
                std::cout << "[command] questions: usage /questions ask <task-id> <text>\n";
                return 0;
            }
            std::string text;
            for (std::size_t i = 2; i < args.size(); ++i) {
                if (i > 2) text.push_back(' ');
                text += args[i];
            }
            try {
                const auto [task, question] = app.task_question_store().ask_question(args[1], text);
                std::cout << "[command] questions ask\n";
                std::cout << "question_id: " << question.id << '\n';
                std::cout << "task_id: " << task.id << '\n';
                std::cout << "status: " << emberforge::system::to_string(task.status) << '\n';
                std::cout << "question: " << question.text << '\n';
            } catch (const std::exception& ex) {
                std::cout << "[command] questions: " << ex.what() << '\n';
            }
            return 0;
        }
        if (action == "answer") {
            if (args.size() < 3) {
                std::cout << "[command] questions: usage /questions answer <question-id> <text>\n";
                return 0;
            }
            std::string answer;
            for (std::size_t i = 2; i < args.size(); ++i) {
                if (i > 2) answer.push_back(' ');
                answer += args[i];
            }
            try {
                const auto [task, question] = app.task_question_store().answer_question(args[1], answer);
                std::cout << "[command] questions answer\n";
                std::cout << "question_id: " << question.id << '\n';
                std::cout << "task_id: " << task.id << '\n';
                std::cout << "task_status: " << emberforge::system::to_string(task.status) << '\n';
                std::cout << "answer: " << (question.answer ? *question.answer : std::string{}) << '\n';
            } catch (const std::exception& ex) {
                std::cout << "[command] questions: " << ex.what() << '\n';
            }
            return 0;
        }
        std::cout << "[command] questions: unsupported action " << action << '\n';
        return 0;
    });

    // /compact — translated placeholder for summarizing session state
    register_handler("compact", [](emberforge::system::StarterSystemApplication& app,
                                   const std::vector<std::string>& /*args*/) -> int {
        const auto report = app.report();
        std::cout << "compact: lifecycle=" << report.lifecycle_state
                  << ", turns=" << report.turn_count
                  << ", handled_requests=" << report.handled_request_count
                  << '\n';
        return 0;
    });

    // /review — translated placeholder for workspace review flow
    register_handler("review", [](emberforge::system::StarterSystemApplication& app,
                                  const std::vector<std::string>& args) -> int {
        const auto scope = args.empty() ? std::string{"workspace"} : [&args]() {
            std::string joined;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) joined.push_back(' ');
                joined += args[i];
            }
            return joined;
        }();
        const auto report = app.report();
        std::cout << "[command] review\n";
        std::cout << "scope: " << scope << '\n';
        std::cout << "commands: " << report.command_count << '\n';
        std::cout << "tools: " << report.tool_count << '\n';
        std::cout << "plugins: " << report.plugin_count << '\n';
        std::cout << "note: starter translation review placeholder\n";
        return 0;
    });

    // /commit — translated placeholder for commit preparation
    register_handler("commit", [](emberforge::system::StarterSystemApplication& app,
                                  const std::vector<std::string>& args) -> int {
        const auto summary = args.empty() ? std::string{"starter translation update"} : [&args]() {
            std::string joined;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) joined.push_back(' ');
                joined += args[i];
            }
            return joined;
        }();
        const auto report = app.report();
        std::cout << "[command] commit\n";
        std::cout << "summary: " << summary << '\n';
        std::cout << "lifecycle: " << report.lifecycle_state << '\n';
        std::cout << "turns: " << report.turn_count << '\n';
        std::cout << "note: starter commit workflow placeholder\n";
        return 0;
    });

    // /pr — translated placeholder for pull request preparation
    register_handler("pr", [](emberforge::system::StarterSystemApplication& app,
                              const std::vector<std::string>& args) -> int {
        const auto context = args.empty() ? std::string{"starter translation update"} : [&args]() {
            std::string joined;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) joined.push_back(' ');
                joined += args[i];
            }
            return joined;
        }();
        const auto report = app.report();
        std::cout << "[command] pr\n";
        std::cout << "context: " << context << '\n';
        std::cout << "commands: " << report.command_count << '\n';
        std::cout << "handled_requests: " << report.handled_request_count << '\n';
        std::cout << "note: starter pull request workflow placeholder\n";
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
