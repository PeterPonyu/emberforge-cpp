#include "emberforge/telemetry/telemetry.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <pwd.h>
#include <stdexcept>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace emberforge::telemetry {

namespace {

std::uint64_t current_timestamp_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string default_session_id() {
    return "session-" + std::to_string(static_cast<long>(getpid())) + "-" +
           std::to_string(current_timestamp_ms());
}

} // namespace

void ConsoleTelemetrySink::record(const Event& event) {
    std::cout << "[telemetry] " << event.name << ": " << event.details << '\n';
}

JsonlTelemetrySink::JsonlTelemetrySink(std::filesystem::path path, std::string session_id)
    : path_(std::move(path)),
      session_id_(session_id.empty() ? default_session_id() : std::move(session_id)) {
    if (const auto parent = path_.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw std::runtime_error("Cannot create telemetry directory: " + parent.string() +
                                     " (" + ec.message() + ")");
        }
    }

    file_.open(path_, std::ios::out | std::ios::app);
    if (!file_) {
        throw std::runtime_error("Cannot open telemetry log for appending: " + path_.string());
    }
}

void JsonlTelemetrySink::record(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json record;
    record["session_id"] = session_id_;
    record["seq"] = sequence_++;
    record["ts"] = current_timestamp_ms();
    record["type"] = event.name;
    record["attrs"] = nlohmann::json::object();
    record["attrs"]["details"] = event.details;

    file_ << record.dump() << '\n';
    file_.flush();
}

std::filesystem::path JsonlTelemetrySink::default_path() {
    const char* explicit_path = std::getenv("EMBER_TELEMETRY_PATH");
    if (explicit_path && explicit_path[0] != '\0') {
        return std::filesystem::path(explicit_path);
    }

    const char* config_home = std::getenv("EMBER_CONFIG_HOME");
    if (config_home && config_home[0] != '\0') {
        return std::filesystem::path(config_home) / "telemetry" / "events.jsonl";
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".emberforge" / "telemetry" / "events.jsonl";
    }

    if (auto* pw = getpwuid(getuid()); pw && pw->pw_dir) {
        return std::filesystem::path(pw->pw_dir) / ".emberforge" / "telemetry" / "events.jsonl";
    }

    return std::filesystem::path(".emberforge") / "telemetry" / "events.jsonl";
}

} // namespace emberforge::telemetry
