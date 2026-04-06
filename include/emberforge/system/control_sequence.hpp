#pragma once

#include <optional>
#include <string>
#include <vector>

#include "emberforge/runtime/runtime.hpp"
#include "emberforge/system/context.hpp"
#include "emberforge/system/dispatch.hpp"
#include "emberforge/system/lifecycle.hpp"
#include "emberforge/telemetry/telemetry.hpp"

namespace emberforge::system {

struct SequenceRecord {
    std::string request_id;
    std::string input;
    DispatchRoute route;
    std::vector<LifecycleState> phases;
    std::string output;
};

class ControlSequenceEngine {
public:
    ControlSequenceEngine(runtime::ConversationRuntime& runtime,
                          const SystemDispatcher& dispatcher,
                          LifecycleTracker& lifecycle,
                          telemetry::TelemetrySink& telemetry);

    void bootstrap();
    [[nodiscard]] SequenceRecord handle(const std::string& input);
    void shutdown();
    [[nodiscard]] std::vector<SequenceRecord> records() const;
    [[nodiscard]] std::optional<SequenceRecord> last_record() const;
    [[nodiscard]] LifecycleState lifecycle_state() const;

private:
    [[nodiscard]] std::string execute_decision(const DispatchDecision& decision);
    [[nodiscard]] std::string render_command_output(const std::string& command_name) const;

    runtime::ConversationRuntime& runtime_;
    const SystemDispatcher& dispatcher_;
    LifecycleTracker& lifecycle_;
    telemetry::TelemetrySink& telemetry_;
    std::size_t next_request_number_{1};
    std::vector<SequenceRecord> records_;
};

} // namespace emberforge::system
