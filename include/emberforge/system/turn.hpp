#pragma once

#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>

#include "emberforge/system/control_sequence.hpp"

// TurnEngine mirrors the responsibility of QueryEngine in claude-code-src
// (claude-code-src/QueryEngine.ts:186): on top of the existing
// ControlSequenceEngine, it owns interruptibility, accumulated usage, and
// per-session budget guardrails for the control sequence layer.
//
// What QueryEngine adds beyond a plain dispatch+execute pipeline:
//   - submit a turn
//   - track tokens / cost
//   - enforce maxTurns / maxBudgetUsd
//   - support interrupt() at any point
//
// The reference is a single 1320-line class; this translation keeps the
// concept but stays small and composable, in the spirit of the existing
// include/emberforge/system layout (one header per concern).

namespace emberforge::system {

struct TurnUsage {
    std::size_t input_tokens{0};
    std::size_t output_tokens{0};
    std::size_t cache_read_tokens{0};
    std::size_t cache_creation_tokens{0};
    double cost_usd{0.0};

    [[nodiscard]] TurnUsage add(const TurnUsage& other) const {
        return TurnUsage{
            input_tokens + other.input_tokens,
            output_tokens + other.output_tokens,
            cache_read_tokens + other.cache_read_tokens,
            cache_creation_tokens + other.cache_creation_tokens,
            cost_usd + other.cost_usd,
        };
    }
};

struct TurnBudget {
    std::size_t max_turns{0};
    double max_cost_usd{0.0};
};

class TurnInterruptedError : public std::runtime_error {
public:
    TurnInterruptedError() : std::runtime_error("turn engine: interrupted") {}
};

class TurnBudgetExceededError : public std::runtime_error {
public:
    TurnBudgetExceededError() : std::runtime_error("turn engine: budget exceeded") {}
};

class TurnEngine {
public:
    TurnEngine(ControlSequenceEngine& sequence, TurnBudget budget);

    SequenceRecord submit(const std::string& input, const TurnUsage& estimated);

    void interrupt();
    void reset();

    [[nodiscard]] TurnUsage total_usage() const;
    [[nodiscard]] std::size_t turns_run() const;

private:
    ControlSequenceEngine& sequence_;
    TurnBudget budget_;
    mutable std::mutex mutex_;
    bool interrupted_{false};
    std::size_t turns_run_{0};
    TurnUsage total_usage_{};
};

} // namespace emberforge::system
