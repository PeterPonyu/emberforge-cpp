#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace emberforge::runtime {

// Model listing & routing, ported for parity with the Rust reference
// (crates/runtime/src/model_router.rs + the ember-cli model report in
// crates/ember-cli/src/main.rs). Two concerns live here:
//   1. Routing strategy — auto / hybrid complexity heuristics plus the fixed
//      default, mirroring Rust parse_strategy / estimate_complexity /
//      select_model. All thresholds and default models are NAMED constants.
//   2. Model-report rendering — the "Available models" report shape the Rust CLI
//      prints (cloud-shortcut aliases + routing shortcuts). The live local-model
//      query lives on OllamaProvider::list_models (the network boundary).

// Complexity heuristic thresholds, mirroring the Rust estimate_complexity
// (model_router.rs:91-94): prompts at or below kSimpleMaxWords (without code
// markers) are Simple; prompts strictly above kComplexMinWords are Complex.
inline constexpr std::size_t kSimpleMaxWords = 5;
inline constexpr std::size_t kComplexMinWords = 50;

// Default model pairs for the auto / hybrid strategies and the fixed fallback,
// mirroring the Rust parse_strategy / RoutingStrategy::default.
inline constexpr const char* kAutoFastModel = "qwen2.5:1.5b";
inline constexpr const char* kAutoCapableModel = "qwen3:8b";
inline constexpr const char* kHybridLocalModel = "qwen3:8b";
inline constexpr const char* kHybridCloudModel = "claude-sonnet-4-6";
inline constexpr const char* kDefaultFixedModel = "qwen3:8b";

// Estimated complexity of a user query, mirroring Rust's TaskComplexity.
enum class TaskComplexity { Simple, Medium, Complex };

// Routing strategy kind, mirroring Rust's RoutingStrategy enum variants.
enum class RoutingStrategyKind { Fixed, Auto, Hybrid };

// A resolved routing strategy. For Fixed only `primary` is meaningful; for Auto
// `primary` is the fast model and `secondary` the capable model; for Hybrid
// `primary` is the local model and `secondary` the cloud model. Mirrors the
// Rust RoutingStrategy enum (collapsed to a tagged struct for C++).
struct RoutingStrategy {
    RoutingStrategyKind kind = RoutingStrategyKind::Fixed;
    std::string primary;    // fixed model / fast model / local model
    std::string secondary;  // capable model / cloud model (unused for Fixed)
};

// Cloud-model alias rows shown under "Cloud shortcuts", mirroring Rust's
// MODEL_ALIAS_ROWS (crates/ember-cli/src/main.rs:73).
inline const std::array<std::pair<const char*, const char*>, 5> kModelAliasRows = {{
    {"opus", "claude-opus-4-6"},
    {"sonnet", "claude-sonnet-4-6"},
    {"haiku", "claude-haiku-4-5-20251213"},
    {"grok", "grok-3"},
    {"grok-mini", "grok-3-mini"},
}};

// Estimate the complexity of a user query from surface-level heuristics,
// mirroring Rust estimate_complexity: short non-code prompts are Simple; code
// markers, multi-step language, or very long prompts are Complex; rest Medium.
[[nodiscard]] TaskComplexity estimate_complexity(const std::string& query);

// Select the model for a query under a strategy, mirroring Rust select_model:
// Auto routes Simple->fast, else->capable; Hybrid routes Simple/Medium->local,
// Complex->cloud; Fixed always returns its model.
[[nodiscard]] std::string select_model(const RoutingStrategy& strategy,
                                       const std::string& query);

// Parse a routing strategy from a model string, mirroring Rust parse_strategy:
// "auto" / "hybrid" map to their default model pairs; anything else is Fixed.
[[nodiscard]] RoutingStrategy parse_strategy(const std::string& model_str);

// Render the "Available models" report, mirroring Rust
// format_available_models_report: Ollama state + local models (current marked
// with `*`), cloud-shortcut aliases, and routing shortcuts. `ollama_status` is a
// human-readable reachability line; `ollama_models` is the live local-tag list
// (empty when unreachable / none).
[[nodiscard]] std::string render_available_models_report(
    const std::string& current_model,
    const std::string& ollama_status,
    const std::vector<std::string>& ollama_models);

}  // namespace emberforge::runtime
