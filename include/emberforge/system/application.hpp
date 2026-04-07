#pragma once

#include <memory>
#include <string>
#include <vector>

#include "emberforge/api/provider.hpp"
#include "emberforge/commands/registry.hpp"
#include "emberforge/compat/upstream_paths.hpp"
#include "emberforge/lsp/manager.hpp"
#include "emberforge/persistence/session_store.hpp"
#include "emberforge/plugins/plugin.hpp"
#include "emberforge/plugins/registry.hpp"
#include "emberforge/runtime/runtime.hpp"
#include "emberforge/server/server.hpp"
#include "emberforge/system/control_sequence.hpp"
#include "emberforge/system/turn.hpp"
#include "emberforge/system/config.hpp"
#include "emberforge/system/dispatch.hpp"
#include "emberforge/system/lifecycle.hpp"
#include "emberforge/system/report.hpp"
#include "emberforge/telemetry/telemetry.hpp"
#include "emberforge/tools/registry.hpp"
#include "emberforge/tools/real_executor.hpp"

namespace emberforge::system {

class StarterSystemApplication {
public:
    // Accept a runtime-chosen provider (ownership transferred via unique_ptr).
    explicit StarterSystemApplication(std::unique_ptr<api::Provider> provider,
                                      StarterSystemConfig config = {});

    [[nodiscard]] std::vector<std::string> run_demo();
    void shutdown();
    [[nodiscard]] StarterSystemReport report() const;
    [[nodiscard]] api::Provider& provider() { return *provider_; }
    [[nodiscard]] persistence::SessionStore& session_store() { return session_store_; }

private:
    StarterSystemConfig config_;
    std::unique_ptr<api::Provider> provider_;
    persistence::SessionStore session_store_;
    tools::RealToolExecutor tool_executor_;
    telemetry::ConsoleTelemetrySink telemetry_;
    runtime::ConversationRuntime runtime_;
    plugins::ExamplePlugin plugin_;
    plugins::PluginRegistry plugin_registry_;
    server::Server server_;
    lsp::LspManager lsp_;
    compat::UpstreamPaths paths_;
    LifecycleTracker lifecycle_;
    SystemDispatcher dispatcher_;
    ControlSequenceEngine control_sequence_;
    TurnEngine turn_;
};

} // namespace emberforge::system
