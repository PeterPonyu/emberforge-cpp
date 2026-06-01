#include "emberforge/telemetry/telemetry.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <nlohmann/json.hpp>

// RAII guard: removes a directory tree on scope exit.
struct TempDirGuard {
    std::filesystem::path path;
    explicit TempDirGuard(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDirGuard() { std::filesystem::remove_all(path); }
};

static std::filesystem::path make_test_dir() {
    return std::filesystem::temp_directory_path() /
           ("emberforge-telemetry-test-" + std::to_string(getpid()));
}

static std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static bool test_jsonl_sink_appends_events() {
    const auto tmp = make_test_dir();
    TempDirGuard guard(tmp);

    const auto log = tmp / "telemetry" / "events.jsonl";
    emberforge::telemetry::JsonlTelemetrySink sink(log, "session-test");

    sink.record({"turn_started", "hello"});
    sink.record({"provider_completed", "world"});

    if (!std::filesystem::exists(log)) {
        std::cerr << "FAIL (jsonl_sink_appends_events): log file was not created\n";
        return false;
    }

    const std::string contents = read_file(log);
    std::istringstream lines(contents);
    std::string line;
    int count = 0;
    std::uint64_t expected_seq = 0;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        const auto record = nlohmann::json::parse(line);
        if (record.value("session_id", "") != "session-test") {
            std::cerr << "FAIL (jsonl_sink_appends_events): session_id mismatch\n";
            return false;
        }
        if (record.value("seq", std::uint64_t{999}) != expected_seq) {
            std::cerr << "FAIL (jsonl_sink_appends_events): seq not monotonic\n";
            return false;
        }
        if (!record.contains("ts") || !record.contains("type") || !record.contains("attrs")) {
            std::cerr << "FAIL (jsonl_sink_appends_events): missing required field\n";
            return false;
        }
        ++expected_seq;
        ++count;
    }

    if (count != 2) {
        std::cerr << "FAIL (jsonl_sink_appends_events): expected 2 events, got " << count << '\n';
        return false;
    }

    std::cout << "PASS (jsonl_sink_appends_events)\n";
    return true;
}

static bool test_jsonl_sink_preserves_existing_lines() {
    const auto tmp = make_test_dir();
    TempDirGuard guard(tmp);

    const auto log = tmp / "events.jsonl";
    {
        emberforge::telemetry::JsonlTelemetrySink sink(log, "s1");
        sink.record({"first", "a"});
    }
    {
        emberforge::telemetry::JsonlTelemetrySink sink(log, "s2");
        sink.record({"second", "b"});
    }

    const std::string contents = read_file(log);
    if (contents.find("\"first\"") == std::string::npos ||
        contents.find("\"second\"") == std::string::npos) {
        std::cerr << "FAIL (jsonl_sink_preserves_existing_lines): append mode did not preserve data\n";
        return false;
    }

    std::cout << "PASS (jsonl_sink_preserves_existing_lines)\n";
    return true;
}

static bool test_default_path_honors_config_home() {
    const auto tmp = make_test_dir();
    TempDirGuard guard(tmp);

    setenv("EMBER_CONFIG_HOME", tmp.c_str(), 1);
    unsetenv("EMBER_TELEMETRY_PATH");

    const auto resolved = emberforge::telemetry::JsonlTelemetrySink::default_path();
    const auto expected = tmp / "telemetry" / "events.jsonl";

    unsetenv("EMBER_CONFIG_HOME");

    if (resolved != expected) {
        std::cerr << "FAIL (default_path_honors_config_home): got '" << resolved.string()
                  << "', expected '" << expected.string() << "'\n";
        return false;
    }

    std::cout << "PASS (default_path_honors_config_home)\n";
    return true;
}

int main() {
    bool all_pass = true;
    all_pass &= test_jsonl_sink_appends_events();
    all_pass &= test_jsonl_sink_preserves_existing_lines();
    all_pass &= test_default_path_honors_config_home();

    if (all_pass) {
        std::cout << "All Telemetry tests PASSED\n";
        return 0;
    }
    return 1;
}
