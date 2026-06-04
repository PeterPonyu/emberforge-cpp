// Tests for the non-interactive one-shot `ember prompt "<text>"` direct loop.
//
// The CLI subcommand (apps/ember_cli/main.cpp) joins the prompt argv into a
// single string and calls StarterSystemApplication::run_prompt() exactly once,
// prints the result, and exits — mirroring the Rust reference
// (`CliAction::Prompt` -> `run_turn_with_output`). These tests exercise both
// layers:
//   1. The run_prompt() primitive: one prompt -> exactly one agent turn through
//      the existing ConversationRuntime, with the prompt reaching the provider.
//   2. The real `ember` binary's `prompt` argument handling (the no-text error
//      path, which needs no Ollama server and so is deterministic in CI).

#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "emberforge/api/provider.hpp"
#include "emberforge/system/application.hpp"

namespace {

int run_capturing(const std::string& command, std::string& captured) {
    captured.clear();
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return -1;
    }
    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        captured += buffer.data();
    }
    const int status = pclose(pipe);
    if (status == -1) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Test 1: prompt_runs_exactly_one_turn
    // A one-shot prompt drives precisely one agent turn through the runtime
    // and the prompt text reaches the provider verbatim.
    // ------------------------------------------------------------------
    {
        auto provider = std::make_unique<emberforge::api::MockProvider>();
        emberforge::system::StarterSystemApplication app(std::move(provider));

        const std::string output = app.run_prompt("explain recursion briefly");

        if (output.find("prompt=explain recursion briefly") == std::string::npos) {
            std::cerr << "FAIL (prompt_runs_exactly_one_turn): provider did not see prompt\n";
            std::cerr << "Output was: " << output << '\n';
            return 1;
        }
        if (app.report().turn_count != 1) {
            std::cerr << "FAIL (prompt_runs_exactly_one_turn): expected 1 turn, got "
                      << app.report().turn_count << '\n';
            return 1;
        }
        std::cout << "PASS (prompt_runs_exactly_one_turn)\n";
    }

    // ------------------------------------------------------------------
    // Test 2: prompt_is_non_interactive_single_shot
    // A fresh application handles one prompt and then shuts down cleanly —
    // no REPL loop, lifecycle reaches a terminal state.
    // ------------------------------------------------------------------
    {
        auto provider = std::make_unique<emberforge::api::MockProvider>();
        emberforge::system::StarterSystemApplication app(std::move(provider));

        const std::string output = app.run_prompt("hello");
        app.shutdown();

        if (output.empty()) {
            std::cerr << "FAIL (prompt_is_non_interactive_single_shot): empty output\n";
            return 1;
        }
        const auto report = app.report();
        if (report.turn_count != 1) {
            std::cerr << "FAIL (prompt_is_non_interactive_single_shot): expected 1 turn, got "
                      << report.turn_count << '\n';
            return 1;
        }
        if (report.lifecycle_state != "stopped") {
            std::cerr << "FAIL (prompt_is_non_interactive_single_shot): expected lifecycle stopped, got "
                      << report.lifecycle_state << '\n';
            return 1;
        }
        std::cout << "PASS (prompt_is_non_interactive_single_shot)\n";
    }

#ifdef EMBER_BINARY
    // ------------------------------------------------------------------
    // Test 3: cli_prompt_subcommand_requires_text
    // The real `ember` binary exposes a `prompt` subcommand. With no text it
    // must report the error and exit non-zero. This path needs no Ollama
    // server, so it deterministically proves the subcommand is wired and that
    // argv is parsed (not silently routed to the REPL or demo mode).
    // ------------------------------------------------------------------
    {
        const std::string binary = EMBER_BINARY;
        std::string captured;
        const int rc = run_capturing(binary + " prompt 2>&1", captured);
        if (rc == 0) {
            std::cerr << "FAIL (cli_prompt_subcommand_requires_text): expected non-zero exit\n";
            std::cerr << "Output was: " << captured << '\n';
            return 1;
        }
        if (captured.find("no text provided") == std::string::npos) {
            std::cerr << "FAIL (cli_prompt_subcommand_requires_text): missing error message\n";
            std::cerr << "Output was: " << captured << '\n';
            return 1;
        }
        std::cout << "PASS (cli_prompt_subcommand_requires_text)\n";
    }
#else
    std::cout << "SKIP (cli_prompt_subcommand_requires_text): EMBER_BINARY not defined\n";
#endif

    std::cout << "All prompt-command tests PASSED\n";
    return 0;
}
