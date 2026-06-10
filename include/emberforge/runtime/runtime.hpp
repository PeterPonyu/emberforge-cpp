#pragma once

#include <cstddef>
#include <string>

#include "emberforge/api/provider.hpp"
#include "emberforge/runtime/session.hpp"
#include "emberforge/telemetry/telemetry.hpp"
#include "emberforge/tools/registry.hpp"

namespace emberforge::runtime {

class ConversationRuntime {
public:
    ConversationRuntime(api::Provider& provider,
                        tools::ToolExecutor& tool_executor,
                        telemetry::TelemetrySink& telemetry_sink);

    // Runs one user turn. For a plain prompt this drives the multi-turn agent
    // loop (mirroring the Rust reference conversation.rs:210-258): the provider
    // may request tools, each is executed via the tool executor and its result
    // fed back, until the model returns no more tool calls — bounded by
    // max_iterations(). `on_delta`, when set, receives streamed text deltas;
    // the returned string is always the accumulated assistant text.
    std::string run_turn(const std::string& input,
                         const api::TextDeltaSink& on_delta = {});
    [[nodiscard]] std::size_t turn_count() const;
    [[nodiscard]] const Session& session() const;

    // The conversation-loop iteration bound (mirrors Rust `max_iterations`).
    // Resolved at construction from EMBER_MAX_ITERATIONS (default 25); the
    // setter exists for deterministic testing of the runaway guard.
    void set_max_iterations(std::size_t max_iterations);
    [[nodiscard]] std::size_t max_iterations() const;

private:
    std::string run_agent_loop(const std::string& input,
                               const api::TextDeltaSink& on_delta);

    api::Provider& provider_;
    tools::ToolExecutor& tool_executor_;
    telemetry::TelemetrySink& telemetry_sink_;
    Session session_;
    std::size_t max_iterations_;
};

} // namespace emberforge::runtime
