// test_ollama_provider.cpp
//
// Unit test for OllamaProvider.
//
// Strategy:
//   1. Spawn a minimal HTTP/1.1 server on an ephemeral POSIX socket in a
//      background std::thread. The server accepts exactly one connection,
//      writes a fixed 3-line NDJSON response, then closes.
//   2. Construct OllamaProvider pointed at 127.0.0.1:<port>.
//   3. Call send_message and assert response.text == "Hello world!".
//
// No external test framework required — plain assert() and return 0/1.

#include "emberforge/api/ollama_provider.hpp"

#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static const char* MOCK_NDJSON =
    "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:00Z\","
    "\"message\":{\"role\":\"assistant\",\"content\":\"Hello \"},\"done\":false}\n"
    "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:01Z\","
    "\"message\":{\"role\":\"assistant\",\"content\":\"world\"},\"done\":false}\n"
    "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:02Z\","
    "\"message\":{\"role\":\"assistant\",\"content\":\"!\"},\"done\":true,"
    "\"total_duration\":123456789}\n";

static void run_mock_server(int server_fd) {
    const std::string body(MOCK_NDJSON);
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/x-ndjson\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
        std::cerr << "mock server: accept() failed\n";
        close(server_fd);
        return;
    }

    char drain_buf[4096];
    while (true) {
        const ssize_t n = recv(client_fd, drain_buf, sizeof(drain_buf), 0);
        if (n <= 0) break;
        // Stop once we've seen the end of HTTP headers (\r\n\r\n).
        // Simple check: look for blank line in what we received.
        const std::string chunk(drain_buf, static_cast<std::size_t>(n));
        if (chunk.find("\r\n\r\n") != std::string::npos) break;
    }

    std::size_t sent = 0;
    while (sent < response.size()) {
        const ssize_t n = send(client_fd, response.c_str() + sent,
                               response.size() - sent, 0);
        if (n < 0) break;
        sent += static_cast<std::size_t>(n);
    }

    close(client_fd);
    close(server_fd);
}

int main() {
    // ------------------------------------------------------------------
    // Check for integration test override: if OLLAMA_BASE_URL is set,
    // run against a live Ollama instance instead of the mock server.
    // ------------------------------------------------------------------
    const char* env_url = std::getenv("OLLAMA_BASE_URL");
    if (env_url != nullptr) {
        std::cout << "Integration mode: using OLLAMA_BASE_URL=" << env_url << "\n";
        const char* env_model = std::getenv("EMBER_MODEL");
        const std::string model = env_model ? env_model : "qwen3:8b";
        emberforge::api::OllamaProvider provider(env_url, model);
        try {
            auto resp = provider.send_message({"test-model", "Say hello in one word."});
            std::cout << "Integration response: " << resp.text << "\n";
            assert(!resp.text.empty() && "Expected non-empty response from live Ollama");
            std::cout << "PASS (integration)\n";
        } catch (const std::exception& ex) {
            std::cerr << "FAIL (integration): " << ex.what() << "\n";
            return 1;
        }
        return 0;
    }

    // Unit test: mock HTTP server
    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "FAIL: socket() failed\n";
        return 1;
    }

    // Allow reuse so re-runs don't hit TIME_WAIT.
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // ephemeral

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "FAIL: bind() failed\n";
        return 1;
    }
    if (listen(server_fd, 1) < 0) {
        std::cerr << "FAIL: listen() failed\n";
        return 1;
    }

    socklen_t addr_len = sizeof(addr);
    getsockname(server_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    const int port = ntohs(addr.sin_port);

    std::thread server_thread(run_mock_server, server_fd);

    const std::string base_url = "http://127.0.0.1:" + std::to_string(port);
    emberforge::api::OllamaProvider provider(base_url, "test-model");

    emberforge::api::MessageResponse resp;
    try {
        resp = provider.send_message({"test-model", "hello"});
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: send_message threw: " << ex.what() << "\n";
        server_thread.join();
        return 1;
    }

    server_thread.join();

    const std::string expected = "Hello world!";
    if (resp.text != expected) {
        std::cerr << "FAIL: expected \"" << expected << "\" but got \""
                  << resp.text << "\"\n";
        return 1;
    }

    std::cout << "PASS: response.text == \"" << resp.text << "\"\n";

    // ------------------------------------------------------------------
    // unicode_escape_decoded: verify \uXXXX is decoded to UTF-8
    // ------------------------------------------------------------------
    // Spin up a second mock server for the unicode test.
    static const char* UNICODE_NDJSON =
        "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:00Z\","
        "\"message\":{\"role\":\"assistant\",\"content\":\"Caf\\u00e9\"},\"done\":false}\n"
        "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:01Z\","
        "\"message\":{\"role\":\"assistant\",\"content\":\"\"},\"done\":true}\n";

    auto run_unicode_server = [](int sfd) {
        const std::string body(UNICODE_NDJSON);
        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/x-ndjson\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        sockaddr_in ca{};
        socklen_t cl = sizeof(ca);
        const int cfd = accept(sfd, reinterpret_cast<sockaddr*>(&ca), &cl);
        if (cfd < 0) { close(sfd); return; }
        char drain[4096];
        while (true) {
            const ssize_t n = recv(cfd, drain, sizeof(drain), 0);
            if (n <= 0) break;
            if (std::string(drain, static_cast<std::size_t>(n)).find("\r\n\r\n") != std::string::npos) break;
        }
        std::size_t sent = 0;
        while (sent < response.size()) {
            const ssize_t n = send(cfd, response.c_str() + sent, response.size() - sent, 0);
            if (n < 0) break;
            sent += static_cast<std::size_t>(n);
        }
        close(cfd);
        close(sfd);
    };

    const int userver_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (userver_fd < 0) { std::cerr << "FAIL (unicode): socket\n"; return 1; }
    int uopt = 1;
    setsockopt(userver_fd, SOL_SOCKET, SO_REUSEADDR, &uopt, sizeof(uopt));
    sockaddr_in uaddr{};
    uaddr.sin_family = AF_INET;
    uaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    uaddr.sin_port = 0;
    if (bind(userver_fd, reinterpret_cast<sockaddr*>(&uaddr), sizeof(uaddr)) < 0) {
        std::cerr << "FAIL (unicode): bind\n"; return 1;
    }
    if (listen(userver_fd, 1) < 0) { std::cerr << "FAIL (unicode): listen\n"; return 1; }
    socklen_t uaddr_len = sizeof(uaddr);
    getsockname(userver_fd, reinterpret_cast<sockaddr*>(&uaddr), &uaddr_len);
    const int uport = ntohs(uaddr.sin_port);

    std::thread unicode_server_thread(run_unicode_server, userver_fd);

    const std::string unicode_base_url = "http://127.0.0.1:" + std::to_string(uport);
    emberforge::api::OllamaProvider unicode_provider(unicode_base_url, "test-model");

    emberforge::api::MessageResponse unicode_resp;
    try {
        unicode_resp = unicode_provider.send_message({"test-model", "hello"});
    } catch (const std::exception& ex) {
        std::cerr << "FAIL (unicode_escape_decoded): threw: " << ex.what() << "\n";
        unicode_server_thread.join();
        return 1;
    }
    unicode_server_thread.join();

    // nlohmann/json decodes \u00e9 to the UTF-8 byte sequence for é (0xC3 0xA9)
    const std::string expected_unicode = "Caf\xc3\xa9";
    if (unicode_resp.text != expected_unicode) {
        std::cerr << "FAIL (unicode_escape_decoded): expected \"Café\" but got \""
                  << unicode_resp.text << "\"\n";
        return 1;
    }
    std::cout << "PASS (unicode_escape_decoded): response.text == \"Café\"\n";

    return 0;
}
