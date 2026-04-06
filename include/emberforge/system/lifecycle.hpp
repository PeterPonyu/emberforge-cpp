#pragma once

#include <string>
#include <vector>

namespace emberforge::system {

enum class LifecycleState {
    Created,
    Bootstrapping,
    Ready,
    Dispatching,
    Executing,
    Persisting,
    Reporting,
    ShuttingDown,
    Stopped,
};

[[nodiscard]] inline std::string to_string(LifecycleState state) {
    switch (state) {
        case LifecycleState::Created:
            return "created";
        case LifecycleState::Bootstrapping:
            return "bootstrapping";
        case LifecycleState::Ready:
            return "ready";
        case LifecycleState::Dispatching:
            return "dispatching";
        case LifecycleState::Executing:
            return "executing";
        case LifecycleState::Persisting:
            return "persisting";
        case LifecycleState::Reporting:
            return "reporting";
        case LifecycleState::ShuttingDown:
            return "shutting_down";
        case LifecycleState::Stopped:
            return "stopped";
    }
    return "unknown";
}

class LifecycleTracker {
public:
    void transition_to(LifecycleState next_state) {
        current_state_ = next_state;
        history_.push_back(next_state);
    }

    [[nodiscard]] LifecycleState current() const {
        return current_state_;
    }

    [[nodiscard]] std::vector<LifecycleState> history() const {
        return history_;
    }

private:
    LifecycleState current_state_{LifecycleState::Created};
    std::vector<LifecycleState> history_{LifecycleState::Created};
};

} // namespace emberforge::system
