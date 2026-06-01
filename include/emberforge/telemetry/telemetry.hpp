#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace emberforge::telemetry {

struct Event {
    std::string name;
    std::string details;
};

class TelemetrySink {
public:
    virtual ~TelemetrySink() = default;
    virtual void record(const Event& event) = 0;
};

class ConsoleTelemetrySink final : public TelemetrySink {
public:
    void record(const Event& event) override;
};

// File-based JSONL telemetry sink that appends one JSON object per event to a
// log under the telemetry directory of the config home. Mirrors the canonical
// Rust `JsonlTelemetrySink` (see crates/telemetry/src/lib.rs): each line carries
// the session id, a monotonically increasing sequence number, a millisecond
// timestamp, the event type/name, and an attributes map. The file is opened in
// append mode so existing telemetry is preserved across sessions.
class JsonlTelemetrySink final : public TelemetrySink {
public:
    // Opens (or creates) the JSONL telemetry log at `path`, creating any missing
    // parent directories. Throws std::runtime_error if the parent directory
    // cannot be created or the log file cannot be opened for appending.
    explicit JsonlTelemetrySink(std::filesystem::path path,
                                std::string session_id = {});

    void record(const Event& event) override;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] const std::string& session_id() const { return session_id_; }

    // Resolves the default telemetry log path under the config home, mirroring
    // the resolution used by other state files (EMBER_CONFIG_HOME, then HOME,
    // then a relative fallback). The log lives in a `telemetry/` subdirectory.
    [[nodiscard]] static std::filesystem::path default_path();

private:
    std::filesystem::path path_;
    std::string session_id_;
    std::ofstream file_;
    std::mutex mutex_;
    std::uint64_t sequence_{0};
};

} // namespace emberforge::telemetry
