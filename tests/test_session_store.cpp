#include "emberforge/persistence/session_store.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

// RAII guard: removes a directory tree on scope exit.
struct TempDirGuard {
    std::filesystem::path path;
    explicit TempDirGuard(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDirGuard() { std::filesystem::remove_all(path); }
};

static std::filesystem::path make_test_dir() {
    return std::filesystem::temp_directory_path() /
           ("emberforge-test-" + std::to_string(getpid()));
}

static bool test_save_and_load_round_trip() {
    const auto tmp = make_test_dir();
    TempDirGuard guard(tmp);

    emberforge::persistence::SessionStore store(tmp);

    emberforge::persistence::Session s;
    s.id = "test-session-001";
    s.created_at = "2026-04-07T00:00:00Z";
    s.messages = {
        {"user",      "Hello, world!",      "2026-04-07T00:01:00Z"},
        {"assistant", "Hi there!",          "2026-04-07T00:01:01Z"},
        {"user",      "How are you?",       "2026-04-07T00:01:02Z"},
    };

    store.save(s);

    emberforge::persistence::SessionStore store2(tmp);
    const auto loaded = store2.load(s.id);

    if (loaded.id != s.id) {
        std::cerr << "FAIL (save_and_load_round_trip): id mismatch\n";
        return false;
    }
    if (loaded.created_at != s.created_at) {
        std::cerr << "FAIL (save_and_load_round_trip): created_at mismatch\n";
        return false;
    }
    if (loaded.messages.size() != s.messages.size()) {
        std::cerr << "FAIL (save_and_load_round_trip): message count mismatch "
                  << loaded.messages.size() << " != " << s.messages.size() << '\n';
        return false;
    }
    for (size_t i = 0; i < s.messages.size(); ++i) {
        if (loaded.messages[i].role != s.messages[i].role ||
            loaded.messages[i].content != s.messages[i].content ||
            loaded.messages[i].timestamp != s.messages[i].timestamp) {
            std::cerr << "FAIL (save_and_load_round_trip): message[" << i << "] mismatch\n";
            return false;
        }
    }

    std::cout << "PASS (save_and_load_round_trip)\n";
    return true;
}

static bool test_list_returns_created_sessions() {
    const auto tmp = make_test_dir();
    TempDirGuard guard(tmp);

    emberforge::persistence::SessionStore store(tmp);

    emberforge::persistence::Session s1;
    s1.id = "alpha";
    s1.created_at = "2026-04-07T00:00:00Z";
    s1.messages = {{"user", "msg1", ""}};

    emberforge::persistence::Session s2;
    s2.id = "beta";
    s2.created_at = "2026-04-07T00:00:01Z";
    s2.messages = {{"user", "msg2", ""}, {"assistant", "reply", ""}};

    store.save(s1);
    store.save(s2);

    const auto summaries = store.list();

    if (summaries.size() != 2) {
        std::cerr << "FAIL (list_returns_created_sessions): expected 2 summaries, got "
                  << summaries.size() << '\n';
        return false;
    }

    bool found_alpha = false, found_beta = false;
    for (const auto& sum : summaries) {
        if (sum.id == "alpha") {
            found_alpha = true;
            if (sum.message_count != 1) {
                std::cerr << "FAIL (list_returns_created_sessions): alpha message_count expected 1, got "
                          << sum.message_count << '\n';
                return false;
            }
        }
        if (sum.id == "beta") {
            found_beta = true;
            if (sum.message_count != 2) {
                std::cerr << "FAIL (list_returns_created_sessions): beta message_count expected 2, got "
                          << sum.message_count << '\n';
                return false;
            }
        }
    }

    if (!found_alpha || !found_beta) {
        std::cerr << "FAIL (list_returns_created_sessions): missing sessions in list\n";
        return false;
    }

    std::cout << "PASS (list_returns_created_sessions)\n";
    return true;
}

static bool test_remove_deletes_file_and_load_throws() {
    const auto tmp = make_test_dir();
    TempDirGuard guard(tmp);

    emberforge::persistence::SessionStore store(tmp);

    emberforge::persistence::Session s;
    s.id = "to-delete";
    s.created_at = "2026-04-07T00:00:00Z";
    s.messages = {{"user", "bye", ""}};

    store.save(s);
    store.remove(s.id);

    bool threw = false;
    try {
        store.load(s.id);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        std::cerr << "FAIL (remove_deletes_file_and_load_throws): load did not throw after remove\n";
        return false;
    }

    std::cout << "PASS (remove_deletes_file_and_load_throws)\n";
    return true;
}

int main() {
    bool all_pass = true;
    all_pass &= test_save_and_load_round_trip();
    all_pass &= test_list_returns_created_sessions();
    all_pass &= test_remove_deletes_file_and_load_throws();

    if (all_pass) {
        std::cout << "All SessionStore tests PASSED\n";
        return 0;
    }
    return 1;
}
