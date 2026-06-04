#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "emberforge/api/provider.hpp"
#include "emberforge/system/application.hpp"

namespace emberforge::system {

namespace {

// Resolve the session permission mode used to gate every tool call in the agent
// loop. Defaults to DangerFullAccess — this is a local CLI operating on the
// invoking user's own workspace, and the canonical bash tool requires it — but
// is overridable via EMBER_PERMISSION_MODE for tighter sandboxes. The gate
// itself is always active (see PermissionToolExecutor); this only selects how
// permissive the session is.
tools::PermissionMode resolve_permission_mode() {
    if (const char* raw = std::getenv("EMBER_PERMISSION_MODE"); raw != nullptr) {
        const std::string mode = raw;
        if (mode == "read-only") {
            return tools::PermissionMode::ReadOnly;
        }
        if (mode == "workspace-write") {
            return tools::PermissionMode::WorkspaceWrite;
        }
        if (mode == "danger-full-access") {
            return tools::PermissionMode::DangerFullAccess;
        }
    }
    return tools::PermissionMode::DangerFullAccess;
}

std::unique_ptr<telemetry::TelemetrySink> make_telemetry_sink() {
    try {
        return std::make_unique<telemetry::JsonlTelemetrySink>(
            telemetry::JsonlTelemetrySink::default_path());
    } catch (const std::exception& ex) {
        std::cerr << "[emberforge] warning: telemetry unavailable (" << ex.what()
                  << "); falling back to console sink\n";
        return std::make_unique<telemetry::ConsoleTelemetrySink>();
    }
}

} // namespace

StarterSystemApplication::StarterSystemApplication(std::unique_ptr<api::Provider> provider,
                                                   StarterSystemConfig config)
    : config_(std::move(config)),
      provider_(std::move(provider)),
      session_store_(std::filesystem::path{}),
      tool_executor_(),
      permission_executor_(tool_executor_, resolve_permission_mode()),
      telemetry_sink_(make_telemetry_sink()),
      telemetry_(*telemetry_sink_),
      runtime_(*provider_, permission_executor_, telemetry_),
      plugin_(),
      plugin_registry_({&plugin_}),
      server_({config_.port}),
      lsp_(),
      paths_(compat::default_upstream_paths()),
      task_question_store_(),
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

std::string StarterSystemApplication::run_prompt(const std::string& text) {
    control_sequence_.bootstrap();
    return control_sequence_.handle(text).output;
}

std::string StarterSystemApplication::run_streaming_prompt(
    const std::string& text, const api::TextDeltaSink& on_delta) {
    // Drive the prompt directly through the runtime's multi-turn agent loop so
    // tokens stream to the caller and tool calls are executed and fed back. The
    // explicit `prompt`/REPL paths are always free-form prompts, so this skips
    // the command/tool dispatch routing of run_prompt by design.
    control_sequence_.bootstrap();
    return runtime_.run_turn(text, on_delta);
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
        .runtime_anchor = paths_.upstream_runtime_ref,
        .turn_count = runtime_.turn_count(),
        .handled_request_count = control_sequence_.records().size(),
        .lifecycle_state = to_string(control_sequence_.lifecycle_state()),
        .last_route = last_record ? std::optional<std::string>{to_string(last_record->route)} : std::nullopt,
        .last_phase_history = last_phase_history,
        .last_turn_input = last_turn ? std::optional<std::string>{last_turn->input} : std::nullopt,
    };
}

} // namespace emberforge::system
