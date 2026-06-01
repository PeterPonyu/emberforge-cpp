#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace emberforge::lsp {

// Configuration describing how to launch and route to a single language server.
// Mirrors the canonical Rust `LspServerConfig` (crates/lsp/src/types.rs).
struct LspServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::string workspace_root;
    // Maps a (normalized, dot-prefixed, lowercase) file extension to an LSP
    // language id, e.g. ".rs" -> "rust".
    std::map<std::string, std::string> extension_to_language;

    [[nodiscard]] std::optional<std::string> language_id_for(const std::string& path) const;
};

// Normalizes a file extension to the canonical ".ext" lowercase form.
[[nodiscard]] std::string normalize_extension(const std::string& extension);

// A live connection to a language server spawned over stdio. Performs
// LSP base-protocol framing (Content-Length headers) and tracks open documents
// for incremental document-sync. Mirrors the canonical Rust `LspClient`
// (crates/lsp/src/client.rs), trimmed to the synchronous subset needed for
// document-sync + hover.
class LspClient {
public:
    ~LspClient();
    LspClient(const LspClient&) = delete;
    LspClient& operator=(const LspClient&) = delete;

    // Spawns the configured server, wires up stdio pipes, and performs the
    // initialize / initialized handshake. Throws std::runtime_error on failure.
    [[nodiscard]] static std::unique_ptr<LspClient> connect(LspServerConfig config);

    void open_document(const std::string& path, const std::string& text);
    void change_document(const std::string& path, const std::string& text);
    void save_document(const std::string& path);
    void close_document(const std::string& path);

    // Sends a textDocument/hover request and returns the rendered hover contents
    // (plain text), or std::nullopt if the server reported no hover.
    [[nodiscard]] std::optional<std::string> hover(const std::string& path,
                                                   std::uint32_t line,
                                                   std::uint32_t character);

    void shutdown();

private:
    explicit LspClient(LspServerConfig config, int stdin_fd, int stdout_fd, long pid);

    void initialize();
    [[nodiscard]] bool is_document_open(const std::string& path) const;

    void notify(const std::string& method, const std::string& params_json);
    [[nodiscard]] std::string request(const std::string& method, const std::string& params_json);
    void send_message(const std::string& payload_json);
    [[nodiscard]] std::optional<std::string> read_message();

    LspServerConfig config_;
    int stdin_fd_{-1};
    int stdout_fd_{-1};
    long pid_{-1};
    bool shutdown_{false};
    std::int64_t next_request_id_{1};
    std::map<std::string, int> open_documents_;
    std::string read_buffer_;
};

// Routes documents to language servers by file extension and owns the lazily
// established client connections. Mirrors the canonical Rust `LspManager`
// (crates/lsp/src/manager.rs).
class LspManager {
public:
    LspManager() = default;
    explicit LspManager(std::vector<LspServerConfig> server_configs);

    [[nodiscard]] std::string summary() const;

    [[nodiscard]] bool supports_path(const std::string& path) const;

    void open_document(const std::string& path, const std::string& text);
    void change_document(const std::string& path, const std::string& text);
    void save_document(const std::string& path);
    void close_document(const std::string& path);
    [[nodiscard]] std::optional<std::string> hover(const std::string& path,
                                                   std::uint32_t line,
                                                   std::uint32_t character);

    void shutdown();

private:
    [[nodiscard]] LspClient& client_for_path(const std::string& path);

    std::map<std::string, LspServerConfig> server_configs_;
    std::map<std::string, std::string> extension_map_;
    std::map<std::string, std::unique_ptr<LspClient>> clients_;
    mutable std::mutex mutex_;
};

} // namespace emberforge::lsp
