#include "emberforge/tools/real_executor.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

namespace {

// Parse `input` as a JSON object when it is non-empty and starts with '{'.
// Returns std::nullopt for non-JSON input so callers can fall back to the
// legacy newline-delimited convention used by the simpler tools.
std::optional<nlohmann::json> try_parse_json_object(const std::string& input) {
    std::size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos || input[start] != '{') {
        return std::nullopt;
    }
    try {
        auto value = nlohmann::json::parse(input);
        if (value.is_object()) {
            return value;
        }
    } catch (const nlohmann::json::exception&) {
    }
    return std::nullopt;
}

std::string json_string_field(const nlohmann::json& obj, const char* key) {
    return obj.value(key, std::string{});
}

} // namespace

std::string RealToolExecutor::execute(const std::string& tool_name, const std::string& input) {
    if (tool_name == "read_file") {
        if (const auto obj = try_parse_json_object(input)) {
            return read_file(json_string_field(*obj, "path"));
        }
        return read_file(input);
    }
    if (tool_name == "write_file") {
        if (const auto obj = try_parse_json_object(input)) {
            return write_file(json_string_field(*obj, "path"),
                              json_string_field(*obj, "content"));
        }
        // legacy input format: "<path>\n<content>"
        const auto newline_pos = input.find('\n');
        if (newline_pos == std::string::npos) {
            return write_file(input, {});
        }
        return write_file(input.substr(0, newline_pos), input.substr(newline_pos + 1));
    }
    if (tool_name == "edit_file") {
        const auto obj = try_parse_json_object(input);
        if (!obj) {
            return "[edit_file] error: expected JSON input with path/old_string/new_string";
        }
        return edit_file(json_string_field(*obj, "path"),
                         json_string_field(*obj, "old_string"),
                         json_string_field(*obj, "new_string"),
                         obj->value("replace_all", false));
    }
    if (tool_name == "glob_search") {
        if (const auto obj = try_parse_json_object(input)) {
            return glob_search(json_string_field(*obj, "pattern"),
                               obj->value("path", std::string{"."}));
        }
        return glob_search(input, ".");
    }
    if (tool_name == "grep_search") {
        if (const auto obj = try_parse_json_object(input)) {
            return grep_search(json_string_field(*obj, "pattern"),
                               obj->value("path", std::string{"."}));
        }
        return grep_search(input, ".");
    }
    if (tool_name == "bash") {
        if (const auto obj = try_parse_json_object(input)) {
            return bash(json_string_field(*obj, "command"));
        }
        return bash(input);
    }
    // Structural / permission-gated tools: these are recognized members of the
    // registry but have no local execution backend in this starter. The
    // PermissionToolExecutor has already authorized the call before dispatch;
    // here we acknowledge it without attempting a network/agent side effect.
    if (tool_name == "web" || tool_name == "notebook" ||
        tool_name == "agent" || tool_name == "skill") {
        return "[" + tool_name + "] accepted (structural tool; no local backend): " + input;
    }
    return "[real_executor] unknown tool: " + tool_name;
}

bool RealToolExecutor::is_within_workspace(const std::string& path) {
    // Boundary check shared by read_file and write_file: a path is inside the
    // workspace when, relative to the cwd, it does not start with "..".
    // May throw std::filesystem::filesystem_error; callers handle it.
    const auto cwd = std::filesystem::current_path();
    const auto canonical_path = std::filesystem::weakly_canonical(path);
    const auto rel = std::filesystem::relative(canonical_path, cwd);
    const std::string rel_str = rel.string();
    return !(!rel_str.empty() && rel_str.rfind("..", 0) == 0);
}

std::string RealToolExecutor::read_file(const std::string& path) {
    // Workspace safety check: refuse paths outside cwd (mirrors write_file).
    try {
        if (!is_within_workspace(path)) {
            return "[read_file] error: path is outside workspace: " + path;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return std::string("[read_file] error: ") + e.what();
    }

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
        if (!is_within_workspace(path)) {
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

std::string RealToolExecutor::edit_file(const std::string& path, const std::string& old_string,
                                        const std::string& new_string, bool replace_all) {
    try {
        if (!is_within_workspace(path)) {
            return "[edit_file] error: path is outside workspace: " + path;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return std::string("[edit_file] error: ") + e.what();
    }

    if (old_string.empty()) {
        return "[edit_file] error: old_string must not be empty";
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return "[edit_file] error: cannot open file: " + path;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    const auto first = content.find(old_string);
    if (first == std::string::npos) {
        return "[edit_file] error: old_string not found in " + path;
    }

    std::size_t replacements = 0;
    if (replace_all) {
        std::size_t pos = 0;
        std::string result;
        result.reserve(content.size());
        while (true) {
            const auto next = content.find(old_string, pos);
            if (next == std::string::npos) {
                result.append(content, pos, std::string::npos);
                break;
            }
            result.append(content, pos, next - pos);
            result.append(new_string);
            pos = next + old_string.size();
            ++replacements;
        }
        content.swap(result);
    } else {
        // Reject ambiguous edits: a non-replace_all edit must match exactly once.
        const auto second = content.find(old_string, first + old_string.size());
        if (second != std::string::npos) {
            return "[edit_file] error: old_string is not unique in " + path +
                   " (use replace_all to replace every occurrence)";
        }
        content.replace(first, old_string.size(), new_string);
        replacements = 1;
    }

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return "[edit_file] error: cannot open file for writing: " + path;
    }
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return "[edit_file] ok: " + std::to_string(replacements) + " replacement(s) in " + path;
}

std::string RealToolExecutor::glob_search(const std::string& pattern, const std::string& root) {
    if (pattern.empty()) {
        return "[glob_search] error: pattern must not be empty";
    }
    try {
        if (!is_within_workspace(root)) {
            return "[glob_search] error: path is outside workspace: " + root;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return std::string("[glob_search] error: ") + e.what();
    }

    // Translate a simple glob (supporting '*' and '?') into a regex matched
    // against each entry's path relative to `root`.
    std::string regex_text;
    regex_text.reserve(pattern.size() * 2);
    for (char c : pattern) {
        switch (c) {
            case '*': regex_text += "[^/]*"; break;
            case '?': regex_text += "[^/]";  break;
            case '.': case '(': case ')': case '+': case '|': case '^':
            case '$': case '{': case '}': case '[': case ']': case '\\':
                regex_text += '\\';
                regex_text += c;
                break;
            default: regex_text += c; break;
        }
    }

    std::regex matcher;
    try {
        matcher.assign(regex_text);
    } catch (const std::regex_error& e) {
        return std::string("[glob_search] error: invalid pattern: ") + e.what();
    }

    std::vector<std::string> matches;
    try {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto rel = std::filesystem::relative(entry.path(), root).generic_string();
            if (std::regex_match(rel, matcher)) {
                matches.push_back(rel);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return std::string("[glob_search] error: ") + e.what();
    }

    std::sort(matches.begin(), matches.end());
    std::string out;
    for (const auto& match : matches) {
        out += match;
        out += '\n';
    }
    return out;
}

std::string RealToolExecutor::grep_search(const std::string& pattern, const std::string& root) {
    if (pattern.empty()) {
        return "[grep_search] error: pattern must not be empty";
    }
    try {
        if (!is_within_workspace(root)) {
            return "[grep_search] error: path is outside workspace: " + root;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return std::string("[grep_search] error: ") + e.what();
    }

    std::regex matcher;
    try {
        matcher.assign(pattern);
    } catch (const std::regex_error& e) {
        return std::string("[grep_search] error: invalid regex: ") + e.what();
    }

    std::string out;
    try {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::ifstream ifs(entry.path());
            if (!ifs.is_open()) {
                continue;
            }
            const auto rel = std::filesystem::relative(entry.path(), root).generic_string();
            std::string line;
            std::size_t line_no = 0;
            while (std::getline(ifs, line)) {
                ++line_no;
                if (std::regex_search(line, matcher)) {
                    out += rel;
                    out += ':';
                    out += std::to_string(line_no);
                    out += ':';
                    out += line;
                    out += '\n';
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return std::string("[grep_search] error: ") + e.what();
    }

    return out;
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
