#include "emberforge/system/application.hpp"

namespace emberforge::system {

StarterSystemApplication::StarterSystemApplication(StarterSystemConfig config)
    : config_(std::move(config)),
      provider_(),
      tool_executor_(),
      telemetry_(),
      runtime_(provider_, tool_executor_, telemetry_),
      plugin_(),
      plugin_registry_({&plugin_}),
      server_({config_.port}),
      lsp_(),
      paths_(compat::default_upstream_paths()),
      lifecycle_(),
      dispatcher_(),
      control_sequence_(runtime_, dispatcher_, lifecycle_, telemetry_),
      turn_(control_sequence_, TurnBudget{config_.max_turns, config_.max_cost_usd}) {}

std::vector<std::string> StarterSystemApplication::run_demo() {
    control_sequence_.bootstrap();
    return {
        control_sequence_.handle("/" + config_.command_demo_name).output,
        control_sequence_.handle(config_.greeting).output,
        control_sequence_.handle("/tool " + config_.tool_demo_command).output,
    };
}

void StarterSystemApplication::shutdown() {
    control_sequence_.shutdown();
}

StarterSystemReport StarterSystemApplication::report() const {
    const auto last_turn = runtime_.session().last_turn();
    const auto last_record = control_sequence_.last_record();
    std::vector<std::string> last_phase_history;
    if (last_record) {
        for (const auto phase : last_record->phases) {
            last_phase_history.push_back(to_string(phase));
        }
    }
    return {
        .app_name = config_.app_name,
        .command_count = commands::get_commands().size(),
        .tool_count = tools::get_tools().size(),
        .plugin_count = plugin_registry_.size(),
        .server_description = server_.describe(),
        .lsp_summary = lsp_.summary(),
        .rust_anchor = paths_.ember_runtime_lib_rs,
        .turn_count = runtime_.turn_count(),
        .handled_request_count = control_sequence_.records().size(),
        .lifecycle_state = to_string(control_sequence_.lifecycle_state()),
        .last_route = last_record ? std::optional<std::string>{to_string(last_record->route)} : std::nullopt,
        .last_phase_history = last_phase_history,
        .last_turn_input = last_turn ? std::optional<std::string>{last_turn->input} : std::nullopt,
    };
}

} // namespace emberforge::system
