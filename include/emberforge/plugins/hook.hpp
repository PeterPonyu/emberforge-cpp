#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "emberforge/plugins/hook_event.hpp"

namespace emberforge::plugins {

// ── Match rules (CROSS_PORT_CONTRACT.md §4.3) ──────────────────────────────
//
// Filters which tool calls trigger a hook. Only meaningful for tool events.
struct HookMatchRule {
    // If non-empty, the tool name must appear in this list. Empty = match all.
    std::vector<std::string> tool_names;
    // Glob patterns for the tool input (e.g. bash commands). A trailing '*'
    // is a prefix-of-substring wildcard, mirroring the Rust reference:
    //   "rm *"       -> input contains "rm "
    //   "git push*"  -> input contains "git push"
    // A bare pattern (no '*') matches when the input contains it. Matching is
    // ASCII case-insensitive, as in crates/plugins/src/hooks.rs.
    std::vector<std::string> commands;

    // Returns true if this rule matches the given tool name and input.
    [[nodiscard]] bool matches(const std::string& tool_name,
                               const std::string& tool_input) const;
};

// Single glob check, exposed for unit testing of the wildcard grammar.
[[nodiscard]] bool glob_match(const std::string& pattern, const std::string& input);

// ── Backends (CROSS_PORT_CONTRACT.md §4.4) ─────────────────────────────────

// Execute a shell command. Exit-code semantics: 0 allow, 2 deny, other warn.
struct CommandBackend {
    std::string run;
};

// POST the hook payload (JSON) to a URL; the response body becomes a message.
struct HttpBackend {
    std::string url;
    std::map<std::string, std::string> headers;
};

using HookBackend = std::variant<CommandBackend, HttpBackend>;

// A structured hook definition (settings.json style, CROSS_PORT_CONTRACT.md §4).
struct HookDefinition {
    HookEvent event{HookEvent::PreToolUse};
    HookBackend backend{CommandBackend{}};
    std::optional<HookMatchRule> match_rule;  // tool events only
    unsigned timeout_secs{30};
    bool run_async{false};
    std::optional<std::string> status_message;
    bool once{false};
};

// ── Hook execution outcome ─────────────────────────────────────────────────

// Per-command outcome derived from the backend exit code / response.
enum class HookDecision {
    Allow,  // exit 0 — proceed (optional captured message)
    Deny,   // exit 2 — block tool execution
    Warn,   // other  — proceed but surface a warning
};

// Aggregate result of running all hooks for one event. Mirrors the Rust
// HookRunResult: a deny short-circuits and is sticky.
struct HookRunResult {
    bool denied{false};
    std::vector<std::string> messages;

    [[nodiscard]] static HookRunResult allow(std::vector<std::string> messages) {
        return HookRunResult{false, std::move(messages)};
    }
    [[nodiscard]] bool is_denied() const { return denied; }
};

} // namespace emberforge::plugins
