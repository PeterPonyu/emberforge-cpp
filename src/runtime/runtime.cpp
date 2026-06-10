#include "emberforge/runtime/runtime.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "emberforge/api/provider.hpp"
#include "emberforge/runtime/system_prompt.hpp"
#include "emberforge/tools/registry.hpp"

namespace emberforge::runtime {

namespace {

// The conversation-loop iteration bound mirrors the Rust reference's
// `max_iterations` (crates/runtime/src/conversation.rs). It is a NAMED,
// documented constant — not a buried literal — and is overridable via
// EMBER_MAX_ITERATIONS for operators who need a tighter or looser bound.
constexpr std::size_t kDefaultMaxIterations = 25;
constexpr const char* kMaxIterationsEnv = "EMBER_MAX_ITERATIONS";

std::size_t resolve_max_iterations() {
    if (const char* raw = std::getenv(kMaxIterationsEnv); raw != nullptr) {
        try {
            const long parsed = std::stol(raw);
            if (parsed > 0) {
                return static_cast<std::size_t>(parsed);
            }
        } catch (const std::exception&) {
            // Malformed value: fall through to the documented default.
        }
    }
    return kDefaultMaxIterations;
}

} // namespace

ConversationRuntime::ConversationRuntime(api::Provider& provider,
                                         tools::ToolExecutor& tool_executor,
                                         telemetry::TelemetrySink& telemetry_sink)
    : provider_(provider),
      tool_executor_(tool_executor),
      telemetry_sink_(telemetry_sink),
      max_iterations_(resolve_max_iterations()) {}

std::string ConversationRuntime::run_turn(const std::string& input,
                                          const api::TextDeltaSink& on_delta) {
    telemetry_sink_.record({"turn_started", input});

    std::string output;

    if (input.rfind("/tool ", 0) == 0) {
        const auto payload = input.substr(6);
        output = tool_executor_.execute("bash", payload);
        telemetry_sink_.record({"tool_executed", output});
    } else {
        output = run_agent_loop(input, on_delta);
    }

    session_.add_turn({input, output});
    return output;
}

std::string ConversationRuntime::run_agent_loop(const std::string& input,
                                                const api::TextDeltaSink& on_delta) {
    // Accumulate the conversation across iterations (parity with the Rust
    // reference's `self.session.messages`). The reused tool registry is sent as
    // the native `tools` array on every turn.
    const auto tool_specs = tools::get_tools();
    const std::string system_prompt = build_runtime_system_prompt();

    std::vector<api::ChatMessage> messages;
    messages.push_back(api::ChatMessage{"user", input, {}, {}});

    std::string final_text;
    std::size_t iterations = 0;

    while (true) {
        ++iterations;
        if (iterations > max_iterations_) {
            telemetry_sink_.record(
                {"max_iterations_exceeded", std::to_string(max_iterations_)});
            break;
        }

        api::ChatRequest request;
        request.system_prompt = system_prompt;
        request.messages = messages;
        request.tools = tool_specs;
        request.on_text_delta = on_delta;

        const api::ChatResult result = provider_.chat(request);

        // Record the assistant turn (text + any tool calls) into the history.
        api::ChatMessage assistant;
        assistant.role = "assistant";
        assistant.content = result.text;
        assistant.tool_calls = result.tool_calls;
        messages.push_back(assistant);
        final_text += result.text;

        if (result.tool_calls.empty()) {
            telemetry_sink_.record({"provider_completed", result.text});
            break;
        }

        // Execute each requested tool via the (permission-gated) executor and
        // append the result as a `tool` role message, then loop and re-send.
        for (const auto& call : result.tool_calls) {
            telemetry_sink_.record({"tool_call_requested", call.name + " " + call.arguments});
            // Surface tool activity to stderr so the interactive transcript shows
            // the loop at work; stdout stays reserved for model/answer content.
            std::cerr << "[tool] " << call.name << " " << call.arguments << '\n';

            const std::string tool_output = tool_executor_.execute(call.name, call.arguments);
            telemetry_sink_.record({"tool_executed", tool_output});

            api::ChatMessage tool_msg;
            tool_msg.role = "tool";
            tool_msg.content = tool_output;
            tool_msg.tool_name = call.name;
            messages.push_back(tool_msg);
        }
    }

    return final_text;
}

void ConversationRuntime::set_max_iterations(std::size_t max_iterations) {
    max_iterations_ = max_iterations;
}

std::size_t ConversationRuntime::max_iterations() const {
    return max_iterations_;
}

std::size_t ConversationRuntime::turn_count() const {
    return session_.turn_count();
}

const Session& ConversationRuntime::session() const {
    return session_;
}

} // namespace emberforge::runtime
