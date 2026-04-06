#pragma once

#include <optional>
#include <string>
#include <vector>

namespace emberforge::runtime {

struct SessionTurn {
    std::string input;
    std::string output;
};

class Session {
public:
    void add_turn(SessionTurn turn);
    [[nodiscard]] std::size_t turn_count() const;
    [[nodiscard]] const std::vector<SessionTurn>& history() const;
    [[nodiscard]] std::optional<SessionTurn> last_turn() const;

private:
    std::vector<SessionTurn> turns_;
};

inline void Session::add_turn(SessionTurn turn) {
    turns_.push_back(std::move(turn));
}

inline std::size_t Session::turn_count() const {
    return turns_.size();
}

inline const std::vector<SessionTurn>& Session::history() const {
    return turns_;
}

inline std::optional<SessionTurn> Session::last_turn() const {
    if (turns_.empty()) {
        return std::nullopt;
    }
    return turns_.back();
}

} // namespace emberforge::runtime
