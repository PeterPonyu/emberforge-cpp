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
      paths_(compat::default_upstream_paths()) {}

std::vector<std::string> StarterSystemApplication::run_demo() {
    return {
        runtime_.run_turn(config_.greeting),
        runtime_.run_turn("/tool " + config_.tool_demo_command),
    };
}

StarterSystemReport StarterSystemApplication::report() const {
    const auto last_turn = runtime_.session().last_turn();
    return {
        .app_name = config_.app_name,
        .command_count = commands::get_commands().size(),
        .tool_count = tools::get_tools().size(),
        .plugin_count = plugin_registry_.size(),
        .server_description = server_.describe(),
        .lsp_summary = lsp_.summary(),
        .rust_anchor = paths_.ember_runtime_lib_rs,
        .turn_count = runtime_.turn_count(),
        .last_turn_input = last_turn ? std::optional<std::string>{last_turn->input} : std::nullopt,
    };
}

} // namespace emberforge::system
