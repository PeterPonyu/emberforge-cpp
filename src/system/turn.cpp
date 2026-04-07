#include "emberforge/system/turn.hpp"

namespace emberforge::system {

TurnEngine::TurnEngine(ControlSequenceEngine& sequence, TurnBudget budget)
    : sequence_(sequence), budget_(budget) {}

SequenceRecord TurnEngine::submit(const std::string& input, const TurnUsage& estimated) {
    TurnUsage projected;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (interrupted_) {
            throw TurnInterruptedError{};
        }
        if (budget_.max_turns > 0 && turns_run_ >= budget_.max_turns) {
            throw TurnBudgetExceededError{};
        }
        projected = total_usage_.add(estimated);
        if (budget_.max_cost_usd > 0.0 && projected.cost_usd > budget_.max_cost_usd) {
            throw TurnBudgetExceededError{};
        }
    }

    auto record = sequence_.handle(input);

    std::lock_guard<std::mutex> lock(mutex_);
    if (interrupted_) {
        throw TurnInterruptedError{};
    }
    ++turns_run_;
    total_usage_ = projected;
    return record;
}

void TurnEngine::interrupt() {
    std::lock_guard<std::mutex> lock(mutex_);
    interrupted_ = true;
}

void TurnEngine::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    interrupted_ = false;
}

TurnUsage TurnEngine::total_usage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_usage_;
}

std::size_t TurnEngine::turns_run() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return turns_run_;
}

} // namespace emberforge::system
