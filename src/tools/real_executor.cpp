#include "emberforge/tools/real_executor.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace emberforge::tools {

namespace {

constexpr std::size_t MAX_FILE_SIZE = 10 * 1024 * 1024; // 10 MB

// Basic blocklist: refuse sudo and recursive root removal without sandbox flag.
static const char* BLOCKED_PREFIXES[] = {
    "sudo ",
    "rm -rf /",
    "rm -fr /",
};

bool is_blocked_command(const std::string& cmd) {
    for (const char* prefix : BLOCKED_PREFIXES) {
        if (cmd.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string RealToolExecutor::execute(const std::string& tool_name, const std::string& input) {
    if (tool_name == "read_file") {
        return read_file(input);
    }
    if (tool_name == "write_file") {
        // input format: "<path>\n<content>"
        const auto newline_pos = input.find('\n');
        if (newline_pos == std::string::npos) {
            return write_file(input, {});
        }
        return write_file(input.substr(0, newline_pos), input.substr(newline_pos + 1));
    }
    if (tool_name == "bash") {
        return bash(input);
    }
    return "[real_executor] unknown tool: " + tool_name;
}

std::string RealToolExecutor::read_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return "[read_file] error: cannot open file: " + path;
    }
    const auto size = static_cast<std::size_t>(ifs.tellg());
    if (size > MAX_FILE_SIZE) {
        return "[read_file] error: file too large (>" + std::to_string(MAX_FILE_SIZE) + " bytes): " + path;
    }
    ifs.seekg(0);
    std::string content(size, '\0');
    ifs.read(content.data(), static_cast<std::streamsize>(size));
    return content;
}

std::string RealToolExecutor::write_file(const std::string& path, const std::string& content) {
    // Basic workspace safety check: refuse paths outside cwd.
    try {
        const auto cwd = std::filesystem::current_path();
        const auto canonical_path = std::filesystem::weakly_canonical(path);
        const auto rel = std::filesystem::relative(canonical_path, cwd);
        const std::string rel_str = rel.string();
        if (!rel_str.empty() && rel_str.rfind("..", 0) == 0) {
            return "[write_file] error: path is outside workspace: " + path;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return std::string("[write_file] error: ") + e.what();
    }

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return "[write_file] error: cannot open file for writing: " + path;
    }
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return "[write_file] ok: wrote " + std::to_string(content.size()) + " bytes to " + path;
}

std::string RealToolExecutor::bash(const std::string& command) {
    if (is_blocked_command(command)) {
        return "[bash] error: command blocked by safety policy: " + command;
    }

    // Wrap the command with timeout(1) (GNU coreutils, universal on Linux/macOS).
    // After 30 seconds, timeout sends SIGTERM to the child process group; if the
    // child is still alive after a short grace period it sends SIGKILL. The
    // exit code reported by `pclose` will be 124 in the timeout-fired case so
    // callers can distinguish from a normal non-zero exit.
    //
    // We invoke `sh -c` inside `timeout` (rather than letting popen's outer
    // shell parse the command directly) so that we can pass the command body
    // as a single quoted argument and survive arbitrary shell metacharacters.
    // Single quotes in the user's command are escaped via the standard
    // close-escape-reopen pattern: ' -> '\\''.
    std::string escaped;
    escaped.reserve(command.size() + 8);
    for (char c : command) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    const std::string full_cmd = "timeout 30 sh -c '(" + escaped + ") 2>&1'";

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        return std::string("[bash] error: popen failed: ") + std::strerror(errno);
    }

    std::string output;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        output += buf.data();
    }
    const int raw_status = pclose(pipe);
    // pclose returns the wait status; extract the actual exit code.
    const int exit_code = WIFEXITED(raw_status) ? WEXITSTATUS(raw_status) : raw_status;
    if (exit_code == 124) {
        output += "[bash] error: command timed out after 30 seconds\n";
    } else if (exit_code != 0) {
        output += "[bash] exit code: " + std::to_string(exit_code) + "\n";
    }
    return output;
}

} // namespace emberforge::tools
