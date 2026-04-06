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

    std::string run_turn(const std::string& input);
    [[nodiscard]] std::size_t turn_count() const;
    [[nodiscard]] const Session& session() const;

private:
    api::Provider& provider_;
    tools::ToolExecutor& tool_executor_;
    telemetry::TelemetrySink& telemetry_sink_;
    Session session_;
};

} // namespace emberforge::runtime
