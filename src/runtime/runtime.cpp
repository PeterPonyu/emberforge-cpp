#include "emberforge/runtime/runtime.hpp"

namespace emberforge::runtime {

ConversationRuntime::ConversationRuntime(api::Provider& provider,
                                         tools::ToolExecutor& tool_executor,
                                         telemetry::TelemetrySink& telemetry_sink)
    : provider_(provider), tool_executor_(tool_executor), telemetry_sink_(telemetry_sink) {}

std::string ConversationRuntime::run_turn(const std::string& input) {
    telemetry_sink_.record({"turn_started", input});

    std::string output;

    if (input.rfind("/tool ", 0) == 0) {
        const auto payload = input.substr(6);
        output = tool_executor_.execute("bash", payload);
        telemetry_sink_.record({"tool_executed", output});
    } else {
        const auto response = provider_.send_message({"claude-sonnet-4-6", input});
        output = response.text;
        telemetry_sink_.record({"provider_completed", output});
    }

    session_.add_turn({input, output});
    return output;
}

std::size_t ConversationRuntime::turn_count() const {
    return session_.turn_count();
}

const Session& ConversationRuntime::session() const {
    return session_;
}

} // namespace emberforge::runtime
