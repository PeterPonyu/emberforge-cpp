// test_system_prompt.cpp
//
// Verifies the ported canonical agent system prompt (parity with the Rust
// reference crates/runtime/src/prompt.rs) and its injection into outgoing
// provider requests.
//
// Coverage:
//   1. build_system_prompt: the five static sections (verbatim) + the dynamic
//      environment section are present, in order, with the named model family.
//   2. OllamaProvider end to end: when MessageRequest carries a system prompt,
//      the request body sent to /api/chat contains a `system` message ahead of
//      the `user` message, and that system message carries the intro marker.
//   3. Hosted providers (Anthropic/xAI) embed the system prompt in their body.
//
// No external test framework — plain asserts and 0/1 return, mirroring the
// other tests in this tree.

#include "emberforge/api/hosted_provider.hpp"
#include "emberforge/api/ollama_provider.hpp"
#include "emberforge/api/provider.hpp"
#include "emberforge/runtime/system_prompt.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

const char* MOCK_NDJSON =
    "{\"model\":\"test-model\",\"created_at\":\"2026-04-07T00:00:00Z\","
    "\"message\":{\"role\":\"assistant\",\"content\":\"ok\"},\"done\":true}\n";

// Mock server that fully drains the request (reads the entire body per the
// Content-Length header) before responding, then captures it for assertions.
// Draining fully matters because the system prompt makes the body large.
void run_body_capturing_server(int sfd, std::string* captured) {
    const std::string body(MOCK_NDJSON);
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

    char drain[8192];
    std::size_t header_end = std::string::npos;
    long content_length = -1;
    while (true) {
        const ssize_t n = recv(cfd, drain, sizeof(drain), 0);
        if (n <= 0) break;
        captured->append(drain, static_cast<std::size_t>(n));
        if (header_end == std::string::npos) {
            header_end = captured->find("\r\n\r\n");
            if (header_end != std::string::npos) {
                const std::string headers = captured->substr(0, header_end);
                const std::string key = "Content-Length:";
                const std::size_t kp = headers.find(key);
                if (kp != std::string::npos) {
                    content_length = std::stol(headers.substr(kp + key.size()));
                }
            }
        }
        if (header_end != std::string::npos && content_length >= 0) {
            const std::size_t have = captured->size() - (header_end + 4);
            if (have >= static_cast<std::size_t>(content_length)) break;
        }
    }

    std::size_t sent = 0;
    while (sent < response.size()) {
        const ssize_t n = send(cfd, response.c_str() + sent, response.size() - sent, 0);
        if (n < 0) break;
        sent += static_cast<std::size_t>(n);
    }
    close(cfd);
    close(sfd);
}

// Bind an ephemeral loopback listening socket; returns fd and sets *port.
int make_listener(int* port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 1) < 0) { close(fd); return -1; }
    socklen_t len = sizeof(addr);
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    *port = ntohs(addr.sin_port);
    return fd;
}

}  // namespace

int main() {
    using emberforge::runtime::build_system_prompt;
    using emberforge::runtime::kFrontierModelName;
    using emberforge::runtime::kSystemPromptIntroMarker;
    using emberforge::runtime::SystemPromptContext;

    // ------------------------------------------------------------------
    // Test 1: build_system_prompt assembles the static sections + env, in the
    // same order as the Rust reference, with the named model family.
    // ------------------------------------------------------------------
    {
        SystemPromptContext ctx;
        ctx.os_name = "Linux";
        ctx.os_version = "6.8.0";
        ctx.cwd = "/home/user/project";
        ctx.date = "2026-06-04";
        const std::string prompt = build_system_prompt(ctx);

        struct Marker { const char* needle; const char* label; };
        const Marker markers[] = {
            {kSystemPromptIntroMarker, "intro line"},
            {"# System", "system section"},
            {"# Doing tasks", "doing-tasks section"},
            {"# Using your tools", "tool-usage section"},
            {"# Executing actions with care", "actions section"},
            {"# Environment context", "environment section"},
            {"Model family: Opus 4.6", "named model family"},
            {"Working directory: /home/user/project", "cwd bullet"},
            {"Date: 2026-06-04", "date bullet"},
            {"Platform: Linux 6.8.0", "platform bullet"},
            // A few verbatim parity anchors from the static sections:
            {"NEVER generate or guess URLs", "intro URL guard"},
            {"flag suspected prompt injection", "system prompt-injection bullet"},
            {"`git status --short --branch`", "tool-usage git hint"},
            {"reversibility and blast radius", "actions blast-radius line"},
        };
        for (const auto& m : markers) {
            if (prompt.find(m.needle) == std::string::npos) {
                std::cerr << "FAIL (build_system_prompt): missing " << m.label
                          << " (\"" << m.needle << "\")\n";
                return 1;
            }
        }

        // Section ordering: intro precedes system precedes actions precedes env.
        const auto p_intro = prompt.find(kSystemPromptIntroMarker);
        const auto p_system = prompt.find("# System");
        const auto p_actions = prompt.find("# Executing actions with care");
        const auto p_env = prompt.find("# Environment context");
        if (!(p_intro < p_system && p_system < p_actions && p_actions < p_env)) {
            std::cerr << "FAIL (build_system_prompt): sections out of order\n";
            return 1;
        }
        // The model family is a named constant, not a buried literal.
        if (std::string(kFrontierModelName) != "Opus 4.6") {
            std::cerr << "FAIL (build_system_prompt): unexpected model family constant\n";
            return 1;
        }
        std::cout << "PASS (build_system_prompt): static sections + env in order\n";
    }

    // ------------------------------------------------------------------
    // Test 2: OllamaProvider injects a `system` message before the `user`
    // message in the request body, carrying the canonical intro marker.
    // ------------------------------------------------------------------
    {
        int port = 0;
        const int fd = make_listener(&port);
        if (fd < 0) { std::cerr << "FAIL (ollama_system_message): listener\n"; return 1; }

        std::string captured;
        std::thread server(run_body_capturing_server, fd, &captured);

        const std::string base_url = "http://127.0.0.1:" + std::to_string(port);
        emberforge::api::OllamaProvider provider(base_url, "qwen3:8b");

        SystemPromptContext ctx;
        ctx.os_name = "Linux";
        ctx.os_version = "6.8.0";
        ctx.cwd = "/tmp";
        ctx.date = "2026-06-04";

        emberforge::api::MessageRequest req;
        req.model = "qwen3:8b";
        req.prompt = "Who are you in one sentence?";
        req.system_prompt = build_system_prompt(ctx);

        try {
            (void)provider.send_message(req);
        } catch (const std::exception& ex) {
            std::cerr << "FAIL (ollama_system_message): send threw: " << ex.what() << "\n";
            server.join();
            return 1;
        }
        server.join();

        const std::size_t hdr = captured.find("\r\n\r\n");
        const std::string req_body =
            hdr == std::string::npos ? std::string{} : captured.substr(hdr + 4);

        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(req_body);
        } catch (const nlohmann::json::exception& ex) {
            std::cerr << "FAIL (ollama_system_message): body is not valid JSON: " << ex.what()
                      << "\nBody was:\n" << req_body << "\n";
            return 1;
        }

        const auto& messages = parsed["messages"];
        if (!messages.is_array() || messages.size() < 2) {
            std::cerr << "FAIL (ollama_system_message): expected >=2 messages\n";
            return 1;
        }
        if (messages[0].value("role", std::string{}) != "system") {
            std::cerr << "FAIL (ollama_system_message): first message is not role=system\n";
            return 1;
        }
        if (messages[0].value("content", std::string{}).find(kSystemPromptIntroMarker) ==
            std::string::npos) {
            std::cerr << "FAIL (ollama_system_message): system message missing intro marker\n";
            return 1;
        }
        if (messages[1].value("role", std::string{}) != "user") {
            std::cerr << "FAIL (ollama_system_message): second message is not role=user\n";
            return 1;
        }
        if (messages[1].value("content", std::string{}) != "Who are you in one sentence?") {
            std::cerr << "FAIL (ollama_system_message): user content not preserved verbatim\n";
            return 1;
        }
        std::cout << "PASS (ollama_system_message): system precedes user, carries intro marker\n";
    }

    // ------------------------------------------------------------------
    // Test 3: hosted providers embed the system prompt in their request body.
    // Pure body construction — no network needed.
    // ------------------------------------------------------------------
    {
        SystemPromptContext ctx;
        ctx.os_name = "Linux";
        ctx.os_version = "6.8.0";
        ctx.cwd = "/tmp";
        ctx.date = "2026-06-04";
        const std::string system_prompt = build_system_prompt(ctx);

        emberforge::api::AnthropicProvider anthropic("key", "claude-opus-4-6");
        const std::string anthropic_body =
            anthropic.build_body(emberforge::api::MessageRequest{"", "hi"}, system_prompt);
        const auto a_json = nlohmann::json::parse(anthropic_body);
        if (!a_json.contains("system") || !a_json["system"].is_array() ||
            a_json["system"][0].value("text", std::string{}).find(kSystemPromptIntroMarker) ==
                std::string::npos) {
            std::cerr << "FAIL (hosted_system_block): Anthropic body missing intro marker\n";
            return 1;
        }

        emberforge::api::xAiProvider xai("key", "grok-2");
        const std::string xai_body =
            xai.build_body(emberforge::api::MessageRequest{"", "hi"}, system_prompt);
        const auto x_json = nlohmann::json::parse(xai_body);
        if (!x_json["messages"].is_array() ||
            x_json["messages"][0].value("role", std::string{}) != "system" ||
            x_json["messages"][0].value("content", std::string{}).find(kSystemPromptIntroMarker) ==
                std::string::npos) {
            std::cerr << "FAIL (hosted_system_block): xAI body missing system intro marker\n";
            return 1;
        }
        std::cout << "PASS (hosted_system_block): Anthropic + xAI embed the system prompt\n";
    }

    std::cout << "All system-prompt tests PASSED\n";
    return 0;
}
