// test_model_router.cpp
//
// Unit tests for the model listing & routing port (parity with the Rust
// reference crates/runtime/src/model_router.rs + the ember-cli model report).
//
// Coverage:
//   1. estimate_complexity: simple / code / multi-step / medium classification.
//   2. select_model / parse_strategy: auto picks fast vs capable by complexity;
//      hybrid picks local vs cloud; fixed is constant.
//   3. OllamaProvider::parse_tags_response: an /api/tags JSON fixture is parsed
//      into the sorted, de-duplicated local-tag list (no network).
//   4. render_available_models_report: report shape (Available models / Cloud
//      shortcuts / Routing shortcuts) with the current model marked.
//
// No external test framework — plain asserts and 0/1 return.

#include "emberforge/api/ollama_provider.hpp"
#include "emberforge/runtime/model_router.hpp"

#include <iostream>
#include <string>
#include <vector>

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

}  // namespace

int main() {
    using namespace emberforge::runtime;

    // ------------------------------------------------------------------
    // estimate_complexity
    // ------------------------------------------------------------------
    check(estimate_complexity("hello") == TaskComplexity::Simple,
          "estimate_complexity: 'hello' is Simple");
    check(estimate_complexity("what time is it") == TaskComplexity::Simple,
          "estimate_complexity: short non-code is Simple");
    check(estimate_complexity("refactor the authentication module to use JWT") ==
              TaskComplexity::Complex,
          "estimate_complexity: code marker -> Complex");
    check(estimate_complexity("implement a REST API with pagination") ==
              TaskComplexity::Complex,
          "estimate_complexity: 'implement' -> Complex");
    check(estimate_complexity(
              "first read the config, then update the database, finally restart") ==
              TaskComplexity::Complex,
          "estimate_complexity: multi-step -> Complex");
    check(estimate_complexity("what files are in the src directory") ==
              TaskComplexity::Medium,
          "estimate_complexity: medium prompt -> Medium");

    // ------------------------------------------------------------------
    // parse_strategy + select_model: /model auto picks fast vs capable.
    // ------------------------------------------------------------------
    {
        const RoutingStrategy fixed = parse_strategy("llama3.1:8b");
        check(fixed.kind == RoutingStrategyKind::Fixed,
              "parse_strategy: bare name -> Fixed");
        check(select_model(fixed, "hello") == "llama3.1:8b" &&
                  select_model(fixed, "implement a database") == "llama3.1:8b",
              "select_model: Fixed always returns its model");

        const RoutingStrategy autos = parse_strategy("auto");
        check(autos.kind == RoutingStrategyKind::Auto,
              "parse_strategy: 'auto' -> Auto");
        const std::string trivial = select_model(autos, "hi");
        const std::string coding = select_model(autos, "refactor the auth module");
        check(trivial == kAutoFastModel,
              "select_model: auto routes trivial prompt to the fast model");
        check(coding == kAutoCapableModel,
              "select_model: auto routes coding prompt to the capable model");
        check(trivial != coding,
              "/model auto: trivial vs coding prompts pick DIFFERENT models");

        const RoutingStrategy hybrid = parse_strategy("hybrid");
        check(hybrid.kind == RoutingStrategyKind::Hybrid,
              "parse_strategy: 'hybrid' -> Hybrid");
        check(select_model(hybrid, "hi") == kHybridLocalModel,
              "select_model: hybrid routes simple -> local");
        check(select_model(hybrid, "architect a distributed cache layer") ==
                  kHybridCloudModel,
              "select_model: hybrid routes complex -> cloud");
    }

    // ------------------------------------------------------------------
    // OllamaProvider::parse_tags_response: parse a /api/tags JSON fixture.
    // ------------------------------------------------------------------
    {
        const std::string fixture = R"({
            "models": [
                {"name": "qwen3:8b", "size": 1},
                {"name": "qwen2.5:1.5b", "size": 2},
                {"name": "qwen3:8b", "size": 1},
                {"name": "llama3.1:8b", "size": 3}
            ]
        })";
        const std::vector<std::string> got =
            emberforge::api::OllamaProvider::parse_tags_response(fixture);
        // Sorted + de-duplicated.
        const std::vector<std::string> want = {"llama3.1:8b", "qwen2.5:1.5b", "qwen3:8b"};
        check(got == want, "parse_tags_response: sorted + de-duplicated tag list");

        // Malformed / missing models field -> empty, no throw.
        check(emberforge::api::OllamaProvider::parse_tags_response("not json").empty(),
              "parse_tags_response: malformed JSON -> empty");
        check(emberforge::api::OllamaProvider::parse_tags_response("{}").empty(),
              "parse_tags_response: missing models field -> empty");
    }

    // ------------------------------------------------------------------
    // render_available_models_report: report shape + current marker.
    // ------------------------------------------------------------------
    {
        const std::vector<std::string> models = {"qwen2.5:1.5b", "qwen3:8b"};
        const std::string report = render_available_models_report(
            "qwen3:8b", "reachable - 2 local model(s) detected", models);
        check(report.find("Available models") != std::string::npos,
              "report: has 'Available models' header");
        check(report.find("Cloud shortcuts") != std::string::npos,
              "report: has 'Cloud shortcuts'");
        check(report.find("Routing shortcuts") != std::string::npos,
              "report: has 'Routing shortcuts'");
        check(report.find("* qwen3:8b") != std::string::npos,
              "report: current model marked with '*'");
        check(report.find("- qwen2.5:1.5b") != std::string::npos,
              "report: other model marked with '-'");
        check(report.find("claude-opus-4-6") != std::string::npos,
              "report: cloud alias rows present");

        // Empty list -> "none listed".
        const std::string empty_report = render_available_models_report(
            "qwen3:8b", "reachable, but no local models were reported", {});
        check(empty_report.find("none listed") != std::string::npos,
              "report: empty local list -> 'none listed'");
    }

    if (failures != 0) {
        std::cerr << failures << " model-router check(s) FAILED\n";
        return 1;
    }
    std::cout << "All model-router tests PASSED\n";
    return 0;
}
