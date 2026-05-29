#pragma once

#include <string>

#include "emberforge/plugins/hook.hpp"

namespace emberforge::plugins {

// Context passed to a backend when a hook fires. Tool fields are empty for
// non-tool (lifecycle) events.
struct HookContext {
    HookEvent event{HookEvent::PreToolUse};
    std::string tool_name;
    std::string tool_input;        // raw JSON string, as it appears on the wire
    std::string tool_output;       // empty unless the event has output
    bool has_output{false};
    bool is_error{false};
};

// Result of invoking one backend: a decision plus an optional human-readable
// message (stdout for command backends, response body for HTTP backends).
struct HookCommandOutcome {
    HookDecision decision{HookDecision::Allow};
    std::string message;
};

// Build the JSON payload (CROSS_PORT_CONTRACT.md §4.4) sent to a backend.
// `tool_input` is embedded both parsed (best-effort) and as the raw string.
[[nodiscard]] std::string build_hook_payload(const HookContext& ctx);

// Abstract executor for a single backend type. Implementations turn a backend
// invocation into a HookCommandOutcome obeying the exit-code/response rules.
class HookExecutor {
public:
    virtual ~HookExecutor() = default;
    [[nodiscard]] virtual HookCommandOutcome run(const HookContext& ctx) const = 0;
};

// ── Command backend (subprocess) ───────────────────────────────────────────
//
// Runs a shell command via `sh -lc <run>` (or executes the file directly if it
// exists on disk), feeds the JSON payload on stdin, and exports HOOK_* env
// vars. Exit-code semantics: 0 allow / 2 deny / other warn.
class CommandHookExecutor final : public HookExecutor {
public:
    explicit CommandHookExecutor(CommandBackend backend, unsigned timeout_secs = 30);

    [[nodiscard]] HookCommandOutcome run(const HookContext& ctx) const override;

    // Map an exit code to a decision (0 allow / 2 deny / other warn). Exposed
    // for unit testing without spawning a process.
    [[nodiscard]] static HookDecision decision_for_exit_code(int code);

private:
    CommandBackend backend_;
    unsigned timeout_secs_;
};

// ── HTTP backend ───────────────────────────────────────────────────────────
//
// Interface for the HTTP backend: POST the JSON payload to a URL with the
// configured headers; the response body becomes the message. The contract maps
// HTTP responses onto the same allow/deny/warn decisions:
//   2xx           -> Allow
//   403/409       -> Deny  (explicit block)
//   other         -> Warn
// A concrete libcurl-backed implementation is a remaining checklist item; the
// interface and decision mapping are defined here so the dispatcher can be
// wired against them.
class HttpHookExecutor : public HookExecutor {
public:
    explicit HttpHookExecutor(HttpBackend backend, unsigned timeout_secs = 30);

    // Map an HTTP status code to a hook decision.
    [[nodiscard]] static HookDecision decision_for_status(long status);

protected:
    HttpBackend backend_;
    unsigned timeout_secs_;
};

} // namespace emberforge::plugins
