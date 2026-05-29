#include "emberforge/plugins/hook_executor.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace emberforge::plugins {

namespace {

// Trim ASCII whitespace from both ends.
std::string trim(const std::string& value) {
    const auto is_space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
    };
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && is_space(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && is_space(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

} // namespace

std::string build_hook_payload(const HookContext& ctx) {
    using nlohmann::json;

    // tool_input is parsed best-effort; if it is not valid JSON we wrap the
    // raw text under a "raw" key, matching parse_tool_input in the Rust port.
    json parsed_input;
    if (ctx.tool_input.empty()) {
        parsed_input = json::object();
    } else {
        try {
            parsed_input = json::parse(ctx.tool_input);
        } catch (const json::parse_error&) {
            parsed_input = json{{"raw", ctx.tool_input}};
        }
    }

    json payload{
        {"hook_event_name", std::string(to_wire_name(ctx.event))},
        {"tool_name", ctx.tool_name},
        {"tool_input", parsed_input},
        {"tool_input_json", ctx.tool_input},
        {"tool_output", ctx.has_output ? json(ctx.tool_output) : json(nullptr)},
        {"tool_result_is_error", ctx.is_error},
    };
    return payload.dump();
}

// ── CommandHookExecutor ────────────────────────────────────────────────────

CommandHookExecutor::CommandHookExecutor(CommandBackend backend, unsigned timeout_secs)
    : backend_(std::move(backend)), timeout_secs_(timeout_secs) {}

HookDecision CommandHookExecutor::decision_for_exit_code(int code) {
    if (code == 0) return HookDecision::Allow;
    if (code == 2) return HookDecision::Deny;
    return HookDecision::Warn;
}

HookCommandOutcome CommandHookExecutor::run(const HookContext& ctx) const {
    const std::string payload = build_hook_payload(ctx);

    // Choose the invocation form: run a file directly if it exists on disk,
    // otherwise treat the string as an inline shell command (`sh -lc`). This
    // mirrors shell_command() in the Rust reference.
    std::error_code exists_ec;
    const bool is_file = std::filesystem::exists(backend_.run, exists_ec) && !exists_ec;

    std::array<int, 2> in_pipe{};   // parent -> child stdin
    std::array<int, 2> out_pipe{};  // child stdout -> parent
    if (::pipe(in_pipe.data()) != 0 || ::pipe(out_pipe.data()) != 0) {
        return HookCommandOutcome{HookDecision::Warn,
                                  "hook `" + backend_.run + "` failed to allocate pipes"};
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(in_pipe[0]); ::close(in_pipe[1]);
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        return HookCommandOutcome{HookDecision::Warn,
                                  "hook `" + backend_.run + "` failed to fork"};
    }

    if (pid == 0) {
        // Child: wire pipes to stdin/stdout, export HOOK_* env, exec the shell.
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::close(in_pipe[0]); ::close(in_pipe[1]);
        ::close(out_pipe[0]); ::close(out_pipe[1]);

        ::setenv("HOOK_EVENT", std::string(to_wire_name(ctx.event)).c_str(), 1);
        ::setenv("HOOK_TOOL_NAME", ctx.tool_name.c_str(), 1);
        ::setenv("HOOK_TOOL_INPUT", ctx.tool_input.c_str(), 1);
        ::setenv("HOOK_TOOL_IS_ERROR", ctx.is_error ? "1" : "0", 1);
        if (ctx.has_output) {
            ::setenv("HOOK_TOOL_OUTPUT", ctx.tool_output.c_str(), 1);
        }

        if (is_file) {
            ::execl("/bin/sh", "sh", backend_.run.c_str(), static_cast<char*>(nullptr));
        } else {
            ::execl("/bin/sh", "sh", "-lc", backend_.run.c_str(), static_cast<char*>(nullptr));
        }
        // exec failed.
        ::_exit(127);
    }

    // Parent: write payload to child stdin, then read stdout to EOF.
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);

    if (!payload.empty()) {
        std::size_t written = 0;
        while (written < payload.size()) {
            const ssize_t n =
                ::write(in_pipe[1], payload.data() + written, payload.size() - written);
            if (n <= 0) break;  // broken pipe is tolerated
            written += static_cast<std::size_t>(n);
        }
    }
    ::close(in_pipe[1]);

    std::string stdout_data;
    {
        std::array<char, 4096> buf{};
        ssize_t n = 0;
        while ((n = ::read(out_pipe[0], buf.data(), buf.size())) > 0) {
            stdout_data.append(buf.data(), static_cast<std::size_t>(n));
        }
    }
    ::close(out_pipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    const std::string message = trim(stdout_data);

    if (WIFSIGNALED(status)) {
        return HookCommandOutcome{
            HookDecision::Warn,
            "hook `" + backend_.run + "` terminated by signal while handling `" +
                ctx.tool_name + "`"};
    }

    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const HookDecision decision = decision_for_exit_code(code);

    if (decision == HookDecision::Warn) {
        std::string warning = "Hook `" + backend_.run + "` exited with status " +
                              std::to_string(code) +
                              "; allowing tool execution to continue";
        if (!message.empty()) {
            warning += ": " + message;
        }
        return HookCommandOutcome{HookDecision::Warn, warning};
    }
    return HookCommandOutcome{decision, message};
}

// ── HttpHookExecutor ───────────────────────────────────────────────────────

HttpHookExecutor::HttpHookExecutor(HttpBackend backend, unsigned timeout_secs)
    : backend_(std::move(backend)), timeout_secs_(timeout_secs) {}

HookDecision HttpHookExecutor::decision_for_status(long status) {
    if (status >= 200 && status < 300) return HookDecision::Allow;
    if (status == 403 || status == 409) return HookDecision::Deny;
    return HookDecision::Warn;
}

} // namespace emberforge::plugins
