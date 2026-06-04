// test_agentic_loop.cpp
//
// Unit tests for the multi-turn tool loop in ConversationRuntime, mirroring the
// Rust reference (crates/runtime/src/conversation.rs:210-258). No network: a
// scripted Provider drives the loop deterministically.
//
// Coverage:
//   1. tool_loop_executes_and_terminates — turn 1 returns a tool_call, turn 2
//      returns final text. Assert: the tool executor ran, the result was
//      appended as a `tool` role message, the loop terminated with the final
//      text, and the request carried the native `tools` array.
//   2. max_iterations_bounds_runaway — a provider that ALWAYS returns a
//      tool_call must stop at max_iterations (no infinite loop).
//
// Plain assert()-style checks; returns 0 on success, 1 on first failure.

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "emberforge/api/provider.hpp"
#include "emberforge/runtime/runtime.hpp"
#include "emberforge/telemetry/telemetry.hpp"
#include "emberforge/tools/registry.hpp"

namespace {

// Telemetry sink that discards events (the loop records several; we don't care).
class NullSink final : public emberforge::telemetry::TelemetrySink {
public:
    void record(const emberforge::telemetry::Event&) override {}
};

// Provider that returns a pre-scripted sequence of ChatResults, one per chat()
// call, and records every request it received for assertions.
class ScriptedProvider final : public emberforge::api::Provider {
public:
    explicit ScriptedProvider(std::vector<emberforge::api::ChatResult> script)
        : script_(std::move(script)) {}

    emberforge::api::MessageResponse send_message(
        const emberforge::api::MessageRequest&) override {
        return emberforge::api::MessageResponse{"unused"};
    }

    emberforge::api::ChatResult chat(const emberforge::api::ChatRequest& request) override {
        requests.push_back(request);
        if (call_index_ < script_.size()) {
            return script_[call_index_++];
        }
        // Past the script: behave like the last scripted result. Used by the
        // runaway test where the final scripted entry keeps requesting a tool.
        return script_.empty() ? emberforge::api::ChatResult{} : script_.back();
    }

    std::vector<emberforge::api::ChatRequest> requests;

private:
    std::vector<emberforge::api::ChatResult> script_;
    std::size_t call_index_{0};
};

// Records each (tool_name, input) pair it is asked to execute.
class RecordingExecutor final : public emberforge::tools::ToolExecutor {
public:
    std::string execute(const std::string& tool_name, const std::string& input) override {
        calls.push_back({tool_name, input});
        return "TOOL_OUTPUT(" + tool_name + ")";
    }

    struct Call {
        std::string name;
        std::string input;
    };
    std::vector<Call> calls;
};

emberforge::api::ToolCall make_tool_call(const std::string& name,
                                         const std::string& arguments) {
    emberforge::api::ToolCall call;
    call.name = name;
    call.arguments = arguments;
    return call;
}

} // namespace

int main() {
    using namespace emberforge;

    // ------------------------------------------------------------------
    // Test 1: tool_loop_executes_and_terminates
    // ------------------------------------------------------------------
    {
        api::ChatResult turn1;  // model requests a tool, no final text yet
        turn1.tool_calls.push_back(make_tool_call("bash", R"({"command":"echo hi"})"));

        api::ChatResult turn2;  // model produces the final answer, no tool calls
        turn2.text = "All done: 1 file.";

        ScriptedProvider provider({turn1, turn2});
        RecordingExecutor executor;
        NullSink sink;
        runtime::ConversationRuntime rt(provider, executor, sink);

        const std::string output = rt.run_turn("list files and count them");

        if (output != "All done: 1 file.") {
            std::cerr << "FAIL (tool_loop): expected final text, got \"" << output << "\"\n";
            return 1;
        }
        // The loop terminated after exactly two provider turns.
        if (provider.requests.size() != 2) {
            std::cerr << "FAIL (tool_loop): expected 2 chat() turns, got "
                      << provider.requests.size() << "\n";
            return 1;
        }
        // The tool actually ran with the model-supplied arguments.
        if (executor.calls.size() != 1 || executor.calls[0].name != "bash" ||
            executor.calls[0].input != R"({"command":"echo hi"})") {
            std::cerr << "FAIL (tool_loop): tool executor did not run as expected\n";
            return 1;
        }
        // The request carried the native tools array (reused registry specs),
        // and it includes the canonical bash tool.
        const auto& tools0 = provider.requests[0].tools;
        if (tools0.empty()) {
            std::cerr << "FAIL (tool_loop): first request carried no tools array\n";
            return 1;
        }
        bool has_bash = false;
        for (const auto& spec : tools0) {
            if (spec.name == "bash") {
                has_bash = true;
                break;
            }
        }
        if (!has_bash) {
            std::cerr << "FAIL (tool_loop): tools array missing the bash spec\n";
            return 1;
        }
        // The second turn's accumulated messages must contain the tool result as
        // a `tool` role message fed back after the assistant's tool-call turn.
        const auto& msgs = provider.requests[1].messages;
        bool found_tool_msg = false;
        for (const auto& m : msgs) {
            if (m.role == "tool" && m.content == "TOOL_OUTPUT(bash)" &&
                m.tool_name == "bash") {
                found_tool_msg = true;
                break;
            }
        }
        if (!found_tool_msg) {
            std::cerr << "FAIL (tool_loop): tool result was not appended as a tool message\n";
            return 1;
        }
        // The runtime recorded exactly one user turn for the whole loop.
        if (rt.turn_count() != 1) {
            std::cerr << "FAIL (tool_loop): expected turn_count 1, got " << rt.turn_count() << "\n";
            return 1;
        }
        std::cout << "PASS (tool_loop_executes_and_terminates)\n";
    }

    // ------------------------------------------------------------------
    // Test 2: max_iterations_bounds_runaway
    // A provider that never stops requesting tools must be bounded.
    // ------------------------------------------------------------------
    {
        api::ChatResult always_tool;
        always_tool.tool_calls.push_back(make_tool_call("bash", R"({"command":"true"})"));

        ScriptedProvider provider({always_tool});  // script repeats the last entry
        RecordingExecutor executor;
        NullSink sink;
        runtime::ConversationRuntime rt(provider, executor, sink);
        rt.set_max_iterations(3);

        const std::string output = rt.run_turn("loop forever");
        (void)output;

        // The loop must perform exactly max_iterations provider turns and stop.
        if (provider.requests.size() != 3) {
            std::cerr << "FAIL (max_iterations): expected 3 bounded turns, got "
                      << provider.requests.size() << "\n";
            return 1;
        }
        // Each of those 3 turns executed the requested tool (3 executions).
        if (executor.calls.size() != 3) {
            std::cerr << "FAIL (max_iterations): expected 3 tool executions, got "
                      << executor.calls.size() << "\n";
            return 1;
        }
        std::cout << "PASS (max_iterations_bounds_runaway)\n";
    }

    std::cout << "All agentic-loop tests PASSED\n";
    return 0;
}
