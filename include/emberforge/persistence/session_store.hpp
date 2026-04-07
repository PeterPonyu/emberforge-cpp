#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace emberforge::persistence {

struct ConversationMessage {
    std::string role;
    std::string content;
    std::string timestamp;
};

struct Session {
    std::string id;
    std::string created_at;
    std::vector<ConversationMessage> messages;
};

struct SessionSummary {
    std::string id;
    size_t message_count;
    std::filesystem::file_time_type last_modified;
};

class SessionStore {
public:
    explicit SessionStore(std::filesystem::path base_dir = "");

    void save(const Session& s);
    Session load(const std::string& id);
    std::vector<SessionSummary> list();
    void remove(const std::string& id);

private:
    std::filesystem::path base_dir_;
    std::filesystem::path path_for(const std::string& id) const;
};

} // namespace emberforge::persistence
