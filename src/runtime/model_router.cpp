#include "emberforge/runtime/model_router.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace emberforge::runtime {

namespace {

// Surface markers that mark a query as code-related (-> Complex), mirroring the
// Rust estimate_complexity has_code_markers check (model_router.rs:79-83).
constexpr std::array<std::string_view, 5> kCodeMarkers = {
    "```", "refactor", "architect", "implement", "design"};

// Multi-step phrasing that marks a query as Complex, mirroring the Rust
// has_multi_step check (model_router.rs:84-89).
constexpr std::array<std::string_view, 6> kMultiStepMarkers = {
    "then", "after that", "step by step", "and also", "first", "finally"};

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t word_count(const std::string& query) {
    std::istringstream stream(query);
    std::string word;
    std::size_t count = 0;
    while (stream >> word) {
        ++count;
    }
    return count;
}

std::string to_lower_trim(const std::string& value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    std::string out;
    for (std::size_t i = begin; i <= end; ++i) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    }
    return out;
}

}  // namespace

TaskComplexity estimate_complexity(const std::string& query) {
    const std::size_t words = word_count(query);
    bool has_code_markers = false;
    for (const auto marker : kCodeMarkers) {
        if (contains(query, marker)) {
            has_code_markers = true;
            break;
        }
    }
    bool has_multi_step = false;
    for (const auto marker : kMultiStepMarkers) {
        if (contains(query, marker)) {
            has_multi_step = true;
            break;
        }
    }

    if (words <= kSimpleMaxWords && !has_code_markers) {
        return TaskComplexity::Simple;
    }
    if (has_code_markers || has_multi_step || words > kComplexMinWords) {
        return TaskComplexity::Complex;
    }
    return TaskComplexity::Medium;
}

std::string select_model(const RoutingStrategy& strategy, const std::string& query) {
    switch (strategy.kind) {
        case RoutingStrategyKind::Fixed:
            return strategy.primary;
        case RoutingStrategyKind::Auto: {
            const TaskComplexity complexity = estimate_complexity(query);
            return complexity == TaskComplexity::Simple ? strategy.primary
                                                        : strategy.secondary;
        }
        case RoutingStrategyKind::Hybrid: {
            const TaskComplexity complexity = estimate_complexity(query);
            return complexity == TaskComplexity::Complex ? strategy.secondary
                                                         : strategy.primary;
        }
    }
    return strategy.primary;
}

RoutingStrategy parse_strategy(const std::string& model_str) {
    const std::string normalized = to_lower_trim(model_str);
    if (normalized == "auto") {
        return RoutingStrategy{RoutingStrategyKind::Auto, kAutoFastModel, kAutoCapableModel};
    }
    if (normalized == "hybrid") {
        return RoutingStrategy{RoutingStrategyKind::Hybrid, kHybridLocalModel,
                               kHybridCloudModel};
    }
    return RoutingStrategy{RoutingStrategyKind::Fixed, model_str, {}};
}

std::string render_available_models_report(const std::string& current_model,
                                           const std::string& ollama_status,
                                           const std::vector<std::string>& ollama_models) {
    std::vector<std::string> lines = {
        "Available models",
        "  Ollama state     " + ollama_status,
    };

    if (ollama_models.empty()) {
        lines.push_back("  Ollama models    none listed");
    } else {
        lines.push_back("  Ollama models");
        for (const auto& model : ollama_models) {
            const std::string marker = (model == current_model) ? "*" : "-";
            lines.push_back("    " + marker + " " + model);
        }
    }

    lines.push_back("Cloud shortcuts");
    for (const auto& [alias, model] : kModelAliasRows) {
        const std::string marker = (model == current_model) ? "*" : "-";
        // Pad the alias to 10 chars, mirroring Rust's `{alias:<10}`.
        std::string padded_alias = alias;
        if (padded_alias.size() < 10) {
            padded_alias.append(10 - padded_alias.size(), ' ');
        }
        lines.push_back("  " + marker + " " + padded_alias + " " + model);
    }

    lines.push_back("Routing shortcuts");
    lines.push_back("  - auto       Route simpler prompts to a faster model");
    lines.push_back("  - hybrid     Prefer local for lighter work, cloud for harder work");

    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            joined += "\n";
        }
        joined += lines[i];
    }
    return joined;
}

}  // namespace emberforge::runtime
