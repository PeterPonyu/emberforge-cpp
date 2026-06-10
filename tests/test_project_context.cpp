// test_project_context.cpp
//
// Unit tests for the dynamic system-prompt project context (parity with the Rust
// reference crates/runtime/src/prompt.rs ProjectContext / render_*).
//
// Coverage:
//   1. discover_instruction_files: an EMBER.md in cwd is discovered; ancestor
//      walk picks up files in parent dirs; identical content is de-duplicated.
//   2. build_system_prompt: a discovered EMBER.md appears (truncated to budget)
//      in the rendered prompt under the "# Emberforge instructions" section,
//      after the dynamic-boundary marker.
//   3. read_git_status / build_system_prompt: a git repo yields a "Git status
//      snapshot" in the project-context section; a non-repo degrades gracefully
//      (no git section, no throw).
//
// No external test framework — plain asserts and 0/1 return.

#include "emberforge/runtime/system_prompt.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool cond, const std::string& label) {
    if (!cond) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    } else {
        std::cout << "PASS: " << label << "\n";
    }
}

// Local helper: a unique temp dir under the system temp root.
fs::path temp_dir() {
    static std::atomic<unsigned long> counter{0};
    const auto nanos = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto seq = counter.fetch_add(1);
    const fs::path dir = fs::temp_directory_path() /
                         ("ember-prompt-ctx-" + std::to_string(nanos) + "-" + std::to_string(seq));
    fs::create_directories(dir);
    return dir;
}

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

}  // namespace

int main() {
    using namespace emberforge::runtime;

    // ------------------------------------------------------------------
    // 1. discover_instruction_files: EMBER.md in cwd + ancestor walk + dedupe.
    // ------------------------------------------------------------------
    {
        const fs::path root = temp_dir();
        const fs::path nested = root / "apps" / "api";
        fs::create_directories(nested);
        write_file(root / "EMBER.md", "Root rules: be precise.");
        write_file(nested / "EMBER.md", "Nested rules: prefer small diffs.");

        const auto files = discover_instruction_files(nested.string());
        check(files.size() == 2,
              "discover: finds EMBER.md in cwd and ancestor (count 2)");
        bool found_root = false, found_nested = false;
        for (const auto& f : files) {
            if (f.content.find("Root rules") != std::string::npos) found_root = true;
            if (f.content.find("Nested rules") != std::string::npos) found_nested = true;
        }
        check(found_root && found_nested, "discover: both files' content present");

        fs::remove_all(root);
    }

    // dedupe identical content across scopes.
    {
        const fs::path root = temp_dir();
        const fs::path nested = root / "apps";
        fs::create_directories(nested);
        write_file(root / "EMBER.md", "same rules\n\n");
        write_file(nested / "EMBER.md", "same rules\n");
        const auto files = discover_instruction_files(nested.string());
        check(files.size() == 1, "discover: identical content de-duplicated");
        fs::remove_all(root);
    }

    // ------------------------------------------------------------------
    // 2. build_system_prompt: EMBER.md appears (truncated) after the boundary.
    // ------------------------------------------------------------------
    {
        const fs::path root = temp_dir();
        // A large EMBER.md to exercise per-file truncation (> 4000 chars).
        const std::string banana = "Always say BANANA first.\n";
        std::string big = banana;
        big += std::string(5000, 'x');
        write_file(root / "EMBER.md", big);

        SystemPromptContext ctx = discover_project_context(root.string(), "2026-06-04");
        ctx.os_name = "Linux";
        ctx.os_version = "6.8.0";
        const std::string prompt = build_system_prompt(ctx);

        check(prompt.find(kSystemPromptDynamicBoundary) != std::string::npos,
              "build_system_prompt: dynamic boundary marker present");
        check(prompt.find("# Emberforge instructions") != std::string::npos,
              "build_system_prompt: instruction section present");
        check(prompt.find("Always say BANANA first.") != std::string::npos,
              "build_system_prompt: EMBER.md content reaches the prompt");
        check(prompt.find("[truncated]") != std::string::npos,
              "build_system_prompt: oversized EMBER.md truncated to budget");
        // The instruction section must come AFTER the boundary marker.
        check(prompt.find("# Emberforge instructions") >
                  prompt.find(kSystemPromptDynamicBoundary),
              "build_system_prompt: instructions injected after the boundary");

        fs::remove_all(root);
    }

    // ------------------------------------------------------------------
    // 3a. Git repo: a status snapshot appears in the project-context section.
    // ------------------------------------------------------------------
    {
        const fs::path root = temp_dir();
        const std::string init_cmd =
            "cd '" + root.string() + "' && git init --quiet 2>/dev/null";
        const int rc = std::system(init_cmd.c_str());
        if (rc == 0) {
            write_file(root / "EMBER.md", "rules");
            write_file(root / "tracked.txt", "hello");

            const auto status = read_git_status(root.string());
            check(status.has_value(), "read_git_status: present in a git repo");
            if (status) {
                check(status->find("EMBER.md") != std::string::npos ||
                          status->find("tracked.txt") != std::string::npos,
                      "read_git_status: untracked files reported");
            }

            SystemPromptContext ctx =
                discover_project_context(root.string(), "2026-06-04");
            const std::string prompt = build_system_prompt(ctx);
            check(prompt.find("Git status snapshot:") != std::string::npos,
                  "build_system_prompt: git status snapshot rendered in a repo");
        } else {
            std::cout << "SKIP: git unavailable for repo status test\n";
        }
        fs::remove_all(root);
    }

    // ------------------------------------------------------------------
    // 3b. Non-repo: degrades gracefully (no git section, no throw).
    // ------------------------------------------------------------------
    {
        const fs::path root = temp_dir();
        write_file(root / "EMBER.md", "rules only, no repo");
        const auto status = read_git_status(root.string());
        check(!status.has_value(),
              "read_git_status: std::nullopt when cwd is not a git repo");

        SystemPromptContext ctx = discover_project_context(root.string(), "2026-06-04");
        const std::string prompt = build_system_prompt(ctx);
        check(prompt.find("Git status snapshot:") == std::string::npos,
              "build_system_prompt: no git section when not a repo (graceful)");
        check(prompt.find("rules only, no repo") != std::string::npos,
              "build_system_prompt: instructions still rendered without a repo");

        fs::remove_all(root);
    }

    if (failures != 0) {
        std::cerr << failures << " project-context check(s) FAILED\n";
        return 1;
    }
    std::cout << "All project-context tests PASSED\n";
    return 0;
}
