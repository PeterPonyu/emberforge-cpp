#include "emberforge/lsp/manager.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>

// RAII guard: removes a path on scope exit.
struct PathGuard {
    std::filesystem::path path;
    explicit PathGuard(std::filesystem::path p) : path(std::move(p)) {}
    ~PathGuard() { std::filesystem::remove_all(path); }
};

static bool test_normalize_extension() {
    using emberforge::lsp::normalize_extension;
    if (normalize_extension("rs") != ".rs" || normalize_extension(".RS") != ".rs" ||
        normalize_extension("CPP") != ".cpp") {
        std::cerr << "FAIL (normalize_extension): unexpected normalization\n";
        return false;
    }
    std::cout << "PASS (normalize_extension)\n";
    return true;
}

static bool test_language_id_for() {
    emberforge::lsp::LspServerConfig config;
    config.name = "rust-analyzer";
    config.extension_to_language = {{".rs", "rust"}};

    const auto rust = config.language_id_for("/tmp/main.rs");
    if (!rust || *rust != "rust") {
        std::cerr << "FAIL (language_id_for): expected rust for .rs\n";
        return false;
    }
    if (config.language_id_for("/tmp/main.py").has_value()) {
        std::cerr << "FAIL (language_id_for): unexpected match for .py\n";
        return false;
    }
    std::cout << "PASS (language_id_for)\n";
    return true;
}

static bool test_manager_routing_and_support() {
    emberforge::lsp::LspServerConfig rust;
    rust.name = "rust-analyzer";
    rust.command = "rust-analyzer";
    rust.extension_to_language = {{".rs", "rust"}};

    emberforge::lsp::LspServerConfig py;
    py.name = "pyright";
    py.command = "pyright-langserver";
    py.extension_to_language = {{".py", "python"}};

    emberforge::lsp::LspManager manager({rust, py});

    if (!manager.supports_path("/work/lib.rs") || !manager.supports_path("/work/app.PY")) {
        std::cerr << "FAIL (manager_routing_and_support): expected supported extensions\n";
        return false;
    }
    if (manager.supports_path("/work/readme.md") || manager.supports_path("/work/noext")) {
        std::cerr << "FAIL (manager_routing_and_support): unexpected support\n";
        return false;
    }

    const auto summary = manager.summary();
    if (summary.find("2 server") == std::string::npos) {
        std::cerr << "FAIL (manager_routing_and_support): summary missing server count: " << summary
                  << '\n';
        return false;
    }

    std::cout << "PASS (manager_routing_and_support)\n";
    return true;
}

static bool test_duplicate_extension_rejected() {
    emberforge::lsp::LspServerConfig a;
    a.name = "server-a";
    a.extension_to_language = {{".ts", "typescript"}};

    emberforge::lsp::LspServerConfig b;
    b.name = "server-b";
    b.extension_to_language = {{".ts", "typescript"}};

    bool threw = false;
    try {
        emberforge::lsp::LspManager manager({a, b});
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        std::cerr << "FAIL (duplicate_extension_rejected): expected throw on duplicate extension\n";
        return false;
    }
    std::cout << "PASS (duplicate_extension_rejected)\n";
    return true;
}

// Spawns a minimal mock LSP server (a shell script speaking the base protocol)
// and exercises the live spawn / initialize / document-sync / hover path.
static bool test_live_document_sync_and_hover() {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("emberforge-lsp-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    PathGuard guard(dir);

    const auto server = dir / "mock_lsp.py";
    {
        std::ofstream out(server);
        out << R"PY(#!/usr/bin/env python3
import sys, json

def read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        line = line.decode("ascii")
        if line in ("\r\n", "\n"):
            break
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    length = int(headers.get("content-length", "0"))
    body = sys.stdin.buffer.read(length)
    return json.loads(body.decode("utf-8"))

def send(payload):
    data = json.dumps(payload).encode("utf-8")
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(data))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()

while True:
    msg = read_message()
    if msg is None:
        break
    method = msg.get("method")
    if "id" in msg:
        if method == "textDocument/hover":
            send({"jsonrpc": "2.0", "id": msg["id"],
                  "result": {"contents": {"kind": "plaintext", "value": "HOVER_OK"}}})
        else:
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {}})
        if method == "shutdown":
            break
)PY";
    }

    emberforge::lsp::LspServerConfig config;
    config.name = "mock";
    config.command = "python3";
    config.args = {server.string()};
    config.workspace_root = dir.string();
    config.extension_to_language = {{".rs", "rust"}};

    emberforge::lsp::LspManager manager({config});

    const auto doc = (dir / "sample.rs").string();
    try {
        manager.open_document(doc, "fn main() {}\n");
        manager.change_document(doc, "fn main() { let x = 1; }\n");
        manager.save_document(doc);
        const auto hover = manager.hover(doc, 0, 3);
        manager.close_document(doc);
        manager.shutdown();

        if (!hover || hover->find("HOVER_OK") == std::string::npos) {
            std::cerr << "FAIL (live_document_sync_and_hover): hover did not return expected value\n";
            return false;
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAIL (live_document_sync_and_hover): exception: " << ex.what() << '\n';
        return false;
    }

    std::cout << "PASS (live_document_sync_and_hover)\n";
    return true;
}

int main() {
    bool all_pass = true;
    all_pass &= test_normalize_extension();
    all_pass &= test_language_id_for();
    all_pass &= test_manager_routing_and_support();
    all_pass &= test_duplicate_extension_rejected();

    // The live path requires python3; treat its absence as a skip rather than a
    // failure so the suite stays green on minimal toolchains.
    if (std::system("python3 --version > /dev/null 2>&1") == 0) {
        all_pass &= test_live_document_sync_and_hover();
    } else {
        std::cout << "SKIP (live_document_sync_and_hover): python3 not available\n";
    }

    if (all_pass) {
        std::cout << "All LspManager tests PASSED\n";
        return 0;
    }
    return 1;
}
