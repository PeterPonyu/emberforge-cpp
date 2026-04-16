#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace emberforge::system {

struct StarterBuddyCompanion {
    std::string name;
    std::string species;
    std::string personality;
    bool muted{false};
};

class StarterBuddyState {
public:
    explicit StarterBuddyState(std::filesystem::path state_path = {});
    [[nodiscard]] std::optional<StarterBuddyCompanion> current() const;
    [[nodiscard]] std::pair<bool, StarterBuddyCompanion> hatch();
    [[nodiscard]] StarterBuddyCompanion rehatch();
    [[nodiscard]] std::optional<StarterBuddyCompanion> mute();
    [[nodiscard]] std::optional<StarterBuddyCompanion> unmute();

private:
    void load();
    void persist() const;

    std::filesystem::path state_path_;
    std::size_t next_index_{0};
    std::optional<StarterBuddyCompanion> companion_;
};

std::string execute_buddy_command(StarterBuddyState& state, const std::string& payload);

} // namespace emberforge::system
