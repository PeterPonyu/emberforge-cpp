#pragma once

#include <string>

namespace emberforge::runtime {

// Canonical agent system prompt, ported for parity with the Rust reference
// (crates/runtime/src/prompt.rs). The five static, model-facing sections are
// embedded verbatim (a system prompt is literal CONTENT, not configuration, so
// embedding the exact text is correct — it is NOT a buried magic literal). Only
// the cheap environment facts (OS, cwd, date) are derived dynamically, and the
// model-family name is a NAMED constant rather than an inline literal.

// Model family surfaced in the environment section. Mirrors the Rust
// reference's FRONTIER_MODEL_NAME constant; kept as a named value, never an
// inline literal buried in the prompt text.
inline constexpr const char* kFrontierModelName = "Opus 4.6";

// A stable line from the intro section. Used by tests (and as a parity anchor)
// to assert the canonical system prompt is present in an outgoing request.
inline constexpr const char* kSystemPromptIntroMarker =
    "You are an interactive agent that helps users with software engineering tasks.";

// Cheap, dynamic environment facts. Everything else in the prompt is canonical
// static content shared byte-for-byte across the ports.
struct SystemPromptContext {
    std::string os_name;
    std::string os_version;
    std::string cwd;
    std::string date;  // ISO date, e.g. "2026-06-04"
};

// Assemble the canonical agent system prompt: the five static sections ported
// verbatim from the Rust reference, in the same order (intro, system, doing
// tasks, tool usage, executing actions), followed by the dynamic environment
// section built from `context`. Sections are joined with a blank line, matching
// the Rust SimpleSystemPromptBuilder::render join.
[[nodiscard]] std::string build_system_prompt(const SystemPromptContext& context);

// Gather the real environment (OS name/version via uname, cwd, today's date)
// and build the canonical system prompt. Used by the live one-shot and REPL
// paths so every turn frames the model as the agent.
[[nodiscard]] std::string build_runtime_system_prompt();

}  // namespace emberforge::runtime
