#include "emberforge/system/control_sequence.hpp"

#include <algorithm>

#include "emberforge/commands/registry.hpp"

namespace emberforge::system {

ControlSequenceEngine::ControlSequenceEngine(runtime::ConversationRuntime& runtime,
                                             const SystemDispatcher& dispatcher,
                                             LifecycleTracker& lifecycle,
                                             telemetry::TelemetrySink& telemetry)
    : runtime_(runtime), dispatcher_(dispatcher), lifecycle_(lifecycle), telemetry_(telemetry) {}

void ControlSequenceEngine::bootstrap() {
    if (lifecycle_.current() != LifecycleState::Created) {
        return;
    }
    lifecycle_.transition_to(LifecycleState::Bootstrapping);
    telemetry_.record({"bootstrap_completed", "system ready"});
    lifecycle_.transition_to(LifecycleState::Ready);
}

SequenceRecord ControlSequenceEngine::handle(const std::string& input) {
    if (lifecycle_.current() == LifecycleState::Created) {
        bootstrap();
    }

    ControlSequenceContext context{
        .request_id = "req-" + std::to_string(next_request_number_++),
        .input = input,
        .route = std::nullopt,
    };

    std::vector<LifecycleState> phases;
    const auto mark = [this, &phases](LifecycleState state) {
        lifecycle_.transition_to(state);
        phases.push_back(state);
    };

    mark(LifecycleState::Dispatching);
    const auto decision = dispatcher_.dispatch(input);
    context.route = to_string(decision.route);

    mark(LifecycleState::Executing);
    const auto output = execute_decision(decision);

    mark(LifecycleState::Persisting);
    records_.push_back({
        .request_id = context.request_id,
        .input = context.input,
        .route = decision.route,
        .phases = phases,
        .output = output,
    });
    telemetry_.record({"sequence_persisted", context.request_id + ":" + to_string(decision.route)});

    mark(LifecycleState::Reporting);
    telemetry_.record({"sequence_reported", output});

    lifecycle_.transition_to(LifecycleState::Ready);
    return records_.back();
}

void ControlSequenceEngine::shutdown() {
    if (lifecycle_.current() == LifecycleState::Stopped) {
        return;
    }
    lifecycle_.transition_to(LifecycleState::ShuttingDown);
    telemetry_.record({"shutdown_completed", "handled=" + std::to_string(records_.size())});
    lifecycle_.transition_to(LifecycleState::Stopped);
}

std::vector<SequenceRecord> ControlSequenceEngine::records() const {
    return records_;
}

std::optional<SequenceRecord> ControlSequenceEngine::last_record() const {
    if (records_.empty()) {
        return std::nullopt;
    }
    return records_.back();
}

LifecycleState ControlSequenceEngine::lifecycle_state() const {
    return lifecycle_.current();
}

std::string ControlSequenceEngine::execute_decision(const DispatchDecision& decision) {
    switch (decision.route) {
        case DispatchRoute::Command:
            return render_command_output(decision.command_name.value_or("unknown"));
        case DispatchRoute::Tool:
            return runtime_.run_turn("/tool " + decision.payload);
        case DispatchRoute::Prompt:
            return runtime_.run_turn(decision.payload);
    }
    return "[sequence] unreachable";
}

std::string ControlSequenceEngine::render_command_output(const std::string& command_name) const {
    if (command_name == "status") {
        return "[command] status: lifecycle=" + to_string(lifecycle_.current()) +
               " handled=" + std::to_string(records_.size());
    }
    if (command_name == "model") {
        return "[command] model: registry-driven control sequence starter";
    }

    const auto commands = commands::get_commands();
    const auto it = std::find_if(commands.begin(), commands.end(), [&command_name](const commands::CommandSpec& command) {
        return command.name == command_name;
    });
    if (it != commands.end()) {
        return "[command] " + it->name + ": " + it->description;
    }
    return "[command] unknown: " + command_name;
}

} // namespace emberforge::system
