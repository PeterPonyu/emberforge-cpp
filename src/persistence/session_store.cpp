#include "emberforge/persistence/session_store.hpp"

#include <cstdlib>
#include <fstream>
#include <pwd.h>
#include <string>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace emberforge::persistence {

namespace {

std::filesystem::path default_base_dir() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".emberforge" / "sessions";
    }
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return std::filesystem::path(pw->pw_dir) / ".emberforge" / "sessions";
    }
    return std::filesystem::path(".emberforge") / "sessions";
}

} // namespace

SessionStore::SessionStore(std::filesystem::path base_dir)
    : base_dir_(base_dir.empty() ? default_base_dir() : std::move(base_dir)) {}

std::filesystem::path SessionStore::path_for(const std::string& id) const {
    return base_dir_ / (id + ".jsonl");
}

void SessionStore::save(const Session& s) {
    std::filesystem::create_directories(base_dir_);

    std::ofstream out(path_for(s.id));
    if (!out) {
        throw std::runtime_error("Cannot open session file for writing: " + s.id);
    }

    nlohmann::json meta;
    meta["type"] = "session";
    meta["id"] = s.id;
    meta["createdAt"] = s.created_at;
    out << meta.dump() << '\n';

    for (const auto& msg : s.messages) {
        nlohmann::json rec;
        rec["type"] = "message";
        rec["role"] = msg.role;
        if (!msg.blocks.is_null() && !msg.blocks.empty()) {
            rec["blocks"] = msg.blocks;
            if (!msg.timestamp.empty()) {
                rec["timestamp"] = msg.timestamp;
            }
        } else {
            rec["content"] = msg.content;
            if (!msg.timestamp.empty()) {
                rec["timestamp"] = msg.timestamp;
            }
        }
        out << rec.dump() << '\n';
    }
}

Session SessionStore::load(const std::string& id) {
    std::ifstream in(path_for(id));
    if (!in) {
        throw std::runtime_error("Session not found: " + id);
    }

    std::string line;
    if (!std::getline(in, line) || line.empty()) {
        throw std::runtime_error("Session file is empty: " + id);
    }

    auto meta = nlohmann::json::parse(line);
    if (meta.value("type", "") != "session") {
        throw std::runtime_error("Invalid session file format: " + id);
    }

    Session session;
    session.id = meta.value("id", id);
    session.created_at = meta.value("createdAt", "");

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto rec = nlohmann::json::parse(line);
        ConversationMessage msg;
        msg.role = rec.value("role", "");
        if (rec.contains("blocks")) {
            msg.blocks = rec["blocks"];
            msg.timestamp = rec.value("timestamp", "");
        } else {
            msg.content = rec.value("content", "");
            msg.timestamp = rec.value("timestamp", "");
        }
        session.messages.push_back(std::move(msg));
    }

    return session;
}

std::vector<SessionSummary> SessionStore::list() {
    std::vector<SessionSummary> summaries;

    if (!std::filesystem::exists(base_dir_)) {
        return summaries;
    }

    for (const auto& entry : std::filesystem::directory_iterator(base_dir_)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".jsonl") continue;

        const std::string id = entry.path().stem().string();

        std::ifstream f(entry.path());
        if (!f) continue;

        size_t line_count = 0;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty()) ++line_count;
        }

        SessionSummary summary;
        summary.id = id;
        summary.message_count = (line_count > 0) ? (line_count - 1) : 0;
        summary.last_modified = entry.last_write_time();
        summaries.push_back(std::move(summary));
    }

    return summaries;
}

void SessionStore::remove(const std::string& id) {
    const auto p = path_for(id);
    if (!std::filesystem::remove(p)) {
        throw std::runtime_error("Session not found: " + id);
    }
}

} // namespace emberforge::persistence
