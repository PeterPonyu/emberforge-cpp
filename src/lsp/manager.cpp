#include "emberforge/lsp/manager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace emberforge::lsp {

namespace {

// Builds a file:// URI for an (absolute) filesystem path. Mirrors the canonical
// `file_url` helper; we keep the encoding minimal since LSP servers accept
// unencoded ASCII paths and the manager operates on already-resolved paths.
std::string file_url(const std::string& path) {
    if (!path.empty() && path.front() == '/') {
        return "file://" + path;
    }
    // Best-effort absolutization for relative inputs.
    std::array<char, 4096> cwd{};
    if (getcwd(cwd.data(), cwd.size()) != nullptr) {
        return std::string("file://") + cwd.data() + "/" + path;
    }
    return "file://" + path;
}

std::string extension_of(const std::string& path) {
    const auto slash = path.find_last_of('/');
    const auto base = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return {};
    }
    return base.substr(dot);
}

ssize_t write_all(int fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        const ssize_t n = ::write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        written += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(written);
}

} // namespace

std::string normalize_extension(const std::string& extension) {
    std::string lowered = extension;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!lowered.empty() && lowered.front() == '.') {
        return lowered;
    }
    return "." + lowered;
}

std::optional<std::string> LspServerConfig::language_id_for(const std::string& path) const {
    const auto ext = extension_of(path);
    if (ext.empty()) {
        return std::nullopt;
    }
    const auto it = extension_to_language.find(normalize_extension(ext));
    if (it == extension_to_language.end()) {
        return std::nullopt;
    }
    return it->second;
}

// ---------------------------------------------------------------------------
// LspClient
// ---------------------------------------------------------------------------

LspClient::LspClient(LspServerConfig config, int stdin_fd, int stdout_fd, long pid)
    : config_(std::move(config)), stdin_fd_(stdin_fd), stdout_fd_(stdout_fd), pid_(pid) {}

LspClient::~LspClient() {
    shutdown();
}

std::unique_ptr<LspClient> LspClient::connect(LspServerConfig config) {
    // A language server may exit (e.g. after `shutdown`) while we still have
    // queued writes to its stdin. Writing to the closed pipe would otherwise
    // raise SIGPIPE and terminate the host process; ignore it so the failed
    // write surfaces as EPIPE through write_all() instead.
    static const bool sigpipe_ignored = [] {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)sigpipe_ignored;

    int in_pipe[2];  // parent writes -> child stdin
    int out_pipe[2]; // child stdout -> parent reads
    if (::pipe2(in_pipe, O_CLOEXEC) != 0 || ::pipe2(out_pipe, O_CLOEXEC) != 0) {
        throw std::runtime_error("LSP: failed to create stdio pipes for " + config.command);
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        throw std::runtime_error("LSP: fork failed for " + config.command);
    }

    if (pid == 0) {
        // Child: wire up stdio and exec the language server.
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);

        if (!config.workspace_root.empty()) {
            if (::chdir(config.workspace_root.c_str()) != 0) {
                // Non-fatal: continue from the current directory.
            }
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(config.command.c_str()));
        for (const auto& arg : config.args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        ::execvp(config.command.c_str(), argv.data());
        // exec failed.
        ::_exit(127);
    }

    // Parent: keep the write end of stdin and the read end of stdout.
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);

    auto client = std::unique_ptr<LspClient>(
        new LspClient(std::move(config), in_pipe[1], out_pipe[0], static_cast<long>(pid)));
    client->initialize();
    return client;
}

void LspClient::initialize() {
    const std::string workspace_uri =
        file_url(config_.workspace_root.empty() ? std::string{"."} : config_.workspace_root);

    nlohmann::json params;
    params["processId"] = static_cast<long>(::getpid());
    params["rootUri"] = workspace_uri;
    params["rootPath"] = config_.workspace_root;
    params["workspaceFolders"] = nlohmann::json::array(
        {{{"uri", workspace_uri}, {"name", config_.name}}});
    params["capabilities"] = {
        {"textDocument",
         {{"synchronization", {{"didSave", true}, {"dynamicRegistration", false}}},
          {"hover", {{"contentFormat", nlohmann::json::array({"plaintext", "markdown"})}}},
          {"publishDiagnostics", {{"relatedInformation", true}}}}},
        {"general", {{"positionEncodings", nlohmann::json::array({"utf-16"})}}},
    };

    (void)request("initialize", params.dump());
    notify("initialized", nlohmann::json::object().dump());
}

bool LspClient::is_document_open(const std::string& path) const {
    return open_documents_.find(path) != open_documents_.end();
}

void LspClient::open_document(const std::string& path, const std::string& text) {
    const auto language_id = config_.language_id_for(path);
    if (!language_id) {
        throw std::runtime_error("LSP: unsupported document (no language id): " + path);
    }

    nlohmann::json params;
    params["textDocument"] = {
        {"uri", file_url(path)},
        {"languageId", *language_id},
        {"version", 1},
        {"text", text},
    };
    notify("textDocument/didOpen", params.dump());
    open_documents_[path] = 1;
}

void LspClient::change_document(const std::string& path, const std::string& text) {
    if (!is_document_open(path)) {
        open_document(path, text);
        return;
    }

    const int next_version = ++open_documents_[path];
    nlohmann::json params;
    params["textDocument"] = {{"uri", file_url(path)}, {"version", next_version}};
    params["contentChanges"] = nlohmann::json::array({{{"text", text}}});
    notify("textDocument/didChange", params.dump());
}

void LspClient::save_document(const std::string& path) {
    if (!is_document_open(path)) {
        return;
    }
    nlohmann::json params;
    params["textDocument"] = {{"uri", file_url(path)}};
    notify("textDocument/didSave", params.dump());
}

void LspClient::close_document(const std::string& path) {
    if (!is_document_open(path)) {
        return;
    }
    nlohmann::json params;
    params["textDocument"] = {{"uri", file_url(path)}};
    notify("textDocument/didClose", params.dump());
    open_documents_.erase(path);
}

std::optional<std::string> LspClient::hover(const std::string& path,
                                            std::uint32_t line,
                                            std::uint32_t character) {
    nlohmann::json params;
    params["textDocument"] = {{"uri", file_url(path)}};
    params["position"] = {{"line", line}, {"character", character}};

    const std::string response = request("textDocument/hover", params.dump());
    if (response.empty()) {
        return std::nullopt;
    }

    const auto parsed = nlohmann::json::parse(response, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("result") || parsed["result"].is_null()) {
        return std::nullopt;
    }

    const auto& contents = parsed["result"]["contents"];
    if (contents.is_string()) {
        return contents.get<std::string>();
    }
    if (contents.is_object() && contents.contains("value")) {
        return contents["value"].get<std::string>();
    }
    if (contents.is_array()) {
        std::string rendered;
        for (const auto& item : contents) {
            if (item.is_string()) {
                rendered += item.get<std::string>();
            } else if (item.is_object() && item.contains("value")) {
                rendered += item["value"].get<std::string>();
            }
            rendered += '\n';
        }
        if (!rendered.empty()) {
            return rendered;
        }
    }
    return std::nullopt;
}

void LspClient::shutdown() {
    if (shutdown_) {
        return;
    }
    shutdown_ = true;

    if (stdin_fd_ >= 0) {
        // Best-effort graceful shutdown; ignore failures from a crashed server.
        try {
            (void)request("shutdown", nlohmann::json::object().dump());
            notify("exit", "null");
        } catch (...) {
            // Server already gone; fall through to process teardown.
        }
        ::close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0) {
        ::close(stdout_fd_);
        stdout_fd_ = -1;
    }
    if (pid_ > 0) {
        int status = 0;
        if (::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG) == 0) {
            ::kill(static_cast<pid_t>(pid_), SIGTERM);
            ::waitpid(static_cast<pid_t>(pid_), &status, 0);
        }
        pid_ = -1;
    }
}

void LspClient::notify(const std::string& method, const std::string& params_json) {
    nlohmann::json message;
    message["jsonrpc"] = "2.0";
    message["method"] = method;
    message["params"] = nlohmann::json::parse(params_json);
    send_message(message.dump());
}

std::string LspClient::request(const std::string& method, const std::string& params_json) {
    const std::int64_t id = next_request_id_++;

    nlohmann::json message;
    message["jsonrpc"] = "2.0";
    message["id"] = id;
    message["method"] = method;
    message["params"] = nlohmann::json::parse(params_json);
    send_message(message.dump());

    // Read messages until the matching response id arrives, draining any
    // server-initiated notifications (e.g. publishDiagnostics) in between.
    while (true) {
        const auto raw = read_message();
        if (!raw) {
            throw std::runtime_error("LSP: connection closed while awaiting response to " + method);
        }
        const auto parsed = nlohmann::json::parse(*raw, nullptr, false);
        if (parsed.is_discarded()) {
            continue;
        }
        if (parsed.contains("id") && parsed["id"].is_number_integer() &&
            parsed["id"].get<std::int64_t>() == id) {
            return *raw;
        }
        // Otherwise a notification or unrelated response: keep draining.
    }
}

void LspClient::send_message(const std::string& payload_json) {
    if (stdin_fd_ < 0) {
        throw std::runtime_error("LSP: cannot send on closed transport");
    }
    const std::string header = "Content-Length: " + std::to_string(payload_json.size()) + "\r\n\r\n";
    if (write_all(stdin_fd_, header.data(), header.size()) < 0 ||
        write_all(stdin_fd_, payload_json.data(), payload_json.size()) < 0) {
        throw std::runtime_error("LSP: failed to write message to server");
    }
}

std::optional<std::string> LspClient::read_message() {
    const auto read_more = [this]() -> bool {
        std::array<char, 4096> chunk{};
        while (true) {
            const ssize_t n = ::read(stdout_fd_, chunk.data(), chunk.size());
            if (n > 0) {
                read_buffer_.append(chunk.data(), static_cast<size_t>(n));
                return true;
            }
            if (n == 0) {
                return false; // EOF
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
    };

    while (true) {
        const auto header_end = read_buffer_.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            if (!read_more()) {
                return std::nullopt;
            }
            continue;
        }

        const std::string headers = read_buffer_.substr(0, header_end);
        size_t content_length = 0;
        bool have_length = false;
        size_t line_start = 0;
        while (line_start <= headers.size()) {
            const auto line_end = headers.find("\r\n", line_start);
            const std::string line =
                headers.substr(line_start, line_end == std::string::npos
                                               ? std::string::npos
                                               : line_end - line_start);
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (name == "content-length") {
                    try {
                        content_length = static_cast<size_t>(std::stoul(line.substr(colon + 1)));
                    } catch (const std::invalid_argument&) {
                        throw std::runtime_error("LSP: invalid Content-Length");
                    } catch (const std::out_of_range&) {
                        throw std::runtime_error("LSP: invalid Content-Length");
                    }
                    have_length = true;
                }
            }
            if (line_end == std::string::npos) {
                break;
            }
            line_start = line_end + 2;
        }

        if (!have_length) {
            throw std::runtime_error("LSP: response missing Content-Length header");
        }

        const size_t body_start = header_end + 4;
        if (read_buffer_.size() < body_start + content_length) {
            if (!read_more()) {
                return std::nullopt;
            }
            continue;
        }

        std::string body = read_buffer_.substr(body_start, content_length);
        read_buffer_.erase(0, body_start + content_length);
        return body;
    }
}

// ---------------------------------------------------------------------------
// LspManager
// ---------------------------------------------------------------------------

LspManager::LspManager(std::vector<LspServerConfig> server_configs) {
    for (auto& config : server_configs) {
        for (const auto& [extension, language] : config.extension_to_language) {
            const auto normalized = normalize_extension(extension);
            const auto [it, inserted] = extension_map_.emplace(normalized, config.name);
            if (!inserted && it->second != config.name) {
                throw std::runtime_error("LSP: extension '" + normalized +
                                         "' mapped to multiple servers ('" + it->second + "' and '" +
                                         config.name + "')");
            }
        }
        server_configs_.emplace(config.name, std::move(config));
    }
}

std::string LspManager::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (server_configs_.empty()) {
        return "C++ LSP manager (stdio JSON-RPC, document-sync + hover)";
    }
    return "C++ LSP manager: " + std::to_string(server_configs_.size()) + " server(s), " +
           std::to_string(extension_map_.size()) + " extension(s)";
}

bool LspManager::supports_path(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto ext = extension_of(path);
    if (ext.empty()) {
        return false;
    }
    return extension_map_.find(normalize_extension(ext)) != extension_map_.end();
}

LspClient& LspManager::client_for_path(const std::string& path) {
    const auto ext = extension_of(path);
    if (ext.empty()) {
        throw std::runtime_error("LSP: document has no extension: " + path);
    }
    const auto ext_it = extension_map_.find(normalize_extension(ext));
    if (ext_it == extension_map_.end()) {
        throw std::runtime_error("LSP: unsupported document: " + path);
    }
    const std::string& server_name = ext_it->second;

    const auto existing = clients_.find(server_name);
    if (existing != clients_.end()) {
        return *existing->second;
    }

    const auto config_it = server_configs_.find(server_name);
    if (config_it == server_configs_.end()) {
        throw std::runtime_error("LSP: unknown server: " + server_name);
    }

    auto client = LspClient::connect(config_it->second);
    auto* raw = client.get();
    clients_.emplace(server_name, std::move(client));
    return *raw;
}

void LspManager::open_document(const std::string& path, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_for_path(path).open_document(path, text);
}

void LspManager::change_document(const std::string& path, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_for_path(path).change_document(path, text);
}

void LspManager::save_document(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_for_path(path).save_document(path);
}

void LspManager::close_document(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_for_path(path).close_document(path);
}

std::optional<std::string> LspManager::hover(const std::string& path,
                                             std::uint32_t line,
                                             std::uint32_t character) {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_for_path(path).hover(path, line, character);
}

void LspManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [name, client] : clients_) {
        if (client) {
            client->shutdown();
        }
    }
    clients_.clear();
}

} // namespace emberforge::lsp
