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

// ---------------------------------------------------------------------------
// Minimal NDJSON response the mock server will send.
// Three deltas: "Hello ", "world", "!" => concatenated: "Hello world!"
// ---------------------------------------------------------------------------
static const char* MOCK_NDJSON =
    "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:00Z\","
    "\"message\":{\"role\":\"assistant\",\"content\":\"Hello \"},\"done\":false}\n"
    "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:01Z\","
    "\"message\":{\"role\":\"assistant\",\"content\":\"world\"},\"done\":false}\n"
    "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:02Z\","
    "\"message\":{\"role\":\"assistant\",\"content\":\"!\"},\"done\":true,"
    "\"total_duration\":123456789}\n";

// ---------------------------------------------------------------------------
// run_mock_server: bind to an ephemeral port, signal the port via out param,
// accept one connection, write the fixed HTTP response, close.
// ---------------------------------------------------------------------------
static void run_mock_server(int server_fd) {
    // Build HTTP/1.1 response wrapping the NDJSON body.
    const std::string body(MOCK_NDJSON);
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/x-ndjson\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    // Accept exactly one connection.
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
        std::cerr << "mock server: accept() failed\n";
        close(server_fd);
        return;
    }

    // Drain the request (we don't care about its contents).
    char drain_buf[4096];
    while (true) {
        const ssize_t n = recv(client_fd, drain_buf, sizeof(drain_buf), 0);
        if (n <= 0) break;
        // Stop once we've seen the end of HTTP headers (\r\n\r\n).
        // Simple check: look for blank line in what we received.
        const std::string chunk(drain_buf, static_cast<std::size_t>(n));
        if (chunk.find("\r\n\r\n") != std::string::npos) break;
    }

    // Send the full response.
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

    // ------------------------------------------------------------------
    // Unit test: mock HTTP server
    // ------------------------------------------------------------------

    // Create a TCP socket and bind to an ephemeral port.
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

    // Retrieve the assigned port.
    socklen_t addr_len = sizeof(addr);
    getsockname(server_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    const int port = ntohs(addr.sin_port);

    // Start the mock server in a background thread.
    std::thread server_thread(run_mock_server, server_fd);

    // Construct provider pointing at the mock server.
    const std::string base_url = "http://127.0.0.1:" + std::to_string(port);
    emberforge::api::OllamaProvider provider(base_url, "test-model");

    // Call send_message.
    emberforge::api::MessageResponse resp;
    try {
        resp = provider.send_message({"test-model", "hello"});
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: send_message threw: " << ex.what() << "\n";
        server_thread.join();
        return 1;
    }

    server_thread.join();

    // Assert the concatenated content matches exactly.
    const std::string expected = "Hello world!";
    if (resp.text != expected) {
        std::cerr << "FAIL: expected \"" << expected << "\" but got \""
                  << resp.text << "\"\n";
        return 1;
    }

    std::cout << "PASS: response.text == \"" << resp.text << "\"\n";
    return 0;
}
