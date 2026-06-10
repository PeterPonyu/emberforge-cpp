#pragma once

#include <optional>
#include <string>
#include <vector>

namespace emberforge::runtime {

// Canonical agent system prompt, ported for parity with the Rust reference
// (crates/runtime/src/prompt.rs). The five static, model-facing sections are
// embedded verbatim (a system prompt is literal CONTENT, not configuration, so
// embedding the exact text is correct — it is NOT a buried magic literal). The
// cheap environment facts (OS, cwd, date) plus the DYNAMIC project context
// (instruction files + git state) discovered after the boundary marker are
// derived dynamically, and budgets / the model-family name are NAMED constants
// rather than inline literals.

// Model family surfaced in the environment section. Mirrors the Rust
// reference's FRONTIER_MODEL_NAME constant; kept as a named value, never an
// inline literal buried in the prompt text.
inline constexpr const char* kFrontierModelName = "Opus 4.6";

// Marker separating the static, model-facing sections from the dynamic context
// (environment, project context, instruction files, config) appended each turn.
// Mirrors the Rust reference's SYSTEM_PROMPT_DYNAMIC_BOUNDARY.
inline constexpr const char* kSystemPromptDynamicBoundary =
    "__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__";

// Instruction-file rendering budgets, mirroring the Rust reference
// (prompt.rs:40-41): at most kMaxInstructionFileChars per file, and at most
// kMaxTotalInstructionChars across all rendered instruction files. NAMED
// constants — not magic literals buried in the render logic.
inline constexpr std::size_t kMaxInstructionFileChars = 4000;
inline constexpr std::size_t kMaxTotalInstructionChars = 12000;

// A stable line from the intro section. Used by tests (and as a parity anchor)
// to assert the canonical system prompt is present in an outgoing request.
inline constexpr const char* kSystemPromptIntroMarker =
    "You are an interactive agent that helps users with software engineering tasks.";

// A single discovered instruction file (EMBER.md / CLAW.md family). Mirrors the
// Rust reference's ContextFile { path, content }.
struct ContextFile {
    std::string path;     // absolute path the file was discovered at
    std::string content;  // raw file content (rendered/truncated at build time)
};

// Cheap, dynamic environment facts plus the discovered project context. The
// static sections of the prompt are canonical content shared byte-for-byte
// across the ports; everything in this struct is derived at runtime.
struct SystemPromptContext {
    std::string os_name;
    std::string os_version;
    std::string cwd;
    std::string date;  // ISO date, e.g. "2026-06-04"

    // Dynamic project context injected after the boundary marker (parity with
    // the Rust reference's ProjectContext). All optional / may be empty.
    std::vector<ContextFile> instruction_files;
    std::optional<std::string> git_status;  // `git status --short --branch`
    std::optional<std::string> git_diff;     // staged + unstaged diffs
};

// Walk `cwd` and every ancestor directory up to the filesystem root, collecting
// the EMBER.md / EMBER.local.md / .ember/* and legacy CLAW.md / .claw/* family
// of instruction files (root-most first), de-duplicating by normalized content.
// Mirrors the Rust reference's discover_instruction_files.
[[nodiscard]] std::vector<ContextFile> discover_instruction_files(const std::string& cwd);

// Run `git --no-optional-locks status --short --branch` in `cwd`. Returns the
// trimmed snapshot, or std::nullopt when `cwd` is not a git repo / git is
// absent / the snapshot is empty. Degrades gracefully (never throws).
[[nodiscard]] std::optional<std::string> read_git_status(const std::string& cwd);

// Collect staged (`git diff --cached`) and unstaged (`git diff`) diffs in `cwd`,
// rendered into labelled sections. Returns std::nullopt when not a git repo /
// git is absent / there are no changes. Degrades gracefully (never throws).
[[nodiscard]] std::optional<std::string> read_git_diff(const std::string& cwd);

// Discover the full dynamic project context for `cwd`: instruction files plus
// git status and diff snapshots. Mirrors ProjectContext::discover_with_git.
[[nodiscard]] SystemPromptContext discover_project_context(const std::string& cwd,
                                                           const std::string& date);

// Assemble the canonical agent system prompt: the five static sections ported
// verbatim from the Rust reference, in order (intro, system, doing tasks, tool
// usage, executing actions), then the dynamic boundary marker, the environment
// section, and — when present — the project-context, instruction-files, and
// config sections built from `context`. Sections are joined with a blank line,
// matching the Rust SystemPromptBuilder::render join.
[[nodiscard]] std::string build_system_prompt(const SystemPromptContext& context);

// Gather the real environment (OS name/version via uname, cwd, today's date)
// AND the dynamic project context (instruction files + git state) from the
// current working directory, then build the canonical system prompt. Used by
// the live one-shot and REPL paths so every turn frames the model as the agent
// with fresh git state and project instructions.
[[nodiscard]] std::string build_runtime_system_prompt();

}  // namespace emberforge::runtime
