// test_thinking.cpp
//
// Unit tests for thinking-token handling (parity with the Rust reference's
// separate-section treatment + the Go/TS thinkSplitter).
//
// Coverage:
//   1. strip_leading_think_block: a well-formed LEADING <think>...</think> block
//      is removed from the answer and captured as thinking.
//   2. A non-leading "<think>" mention is NOT mangled (left in the answer).
//   3. The streaming separator splits content fed across chunk boundaries,
//      holding back a partial closing tag.
//   4. add_structured_thinking accumulates the message.thinking channel.
//   5. show_thinking() reflects the EMBER_SHOW_THINKING env flag (default off).
//
// No external test framework — plain asserts and 0/1 return.

#include "emberforge/api/ollama_provider.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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
    using emberforge::api::show_thinking;
    using emberforge::api::strip_leading_think_block;
    using emberforge::api::ThinkStreamSeparator;

    // ------------------------------------------------------------------
    // 1. Leading <think> block is stripped; answer is clean, reasoning captured.
    // ------------------------------------------------------------------
    {
        std::string thinking;
        const std::string answer = strip_leading_think_block(
            "<think>Let me reason about this.</think>\nThe answer is 42.", thinking);
        check(answer == "The answer is 42.",
              "strip_leading_think_block: answer is clean (no <think> leakage)");
        check(thinking == "Let me reason about this.",
              "strip_leading_think_block: reasoning captured separately");
        check(answer.find("<think>") == std::string::npos &&
                  answer.find("</think>") == std::string::npos,
              "strip_leading_think_block: no tag leakage in answer");
    }

    // ------------------------------------------------------------------
    // 2. Leading whitespace before <think> is tolerated.
    // ------------------------------------------------------------------
    {
        std::string thinking;
        const std::string answer =
            strip_leading_think_block("  \n<think>hmm</think>final", thinking);
        check(answer == "final", "leading-whitespace <think> still stripped");
        check(thinking == "hmm", "leading-whitespace reasoning captured");
    }

    // ------------------------------------------------------------------
    // 3. A NON-leading <think> mention is NOT mangled.
    // ------------------------------------------------------------------
    {
        std::string thinking;
        const std::string content = "Here is code: if (x < y) <think> is a tag.";
        const std::string answer = strip_leading_think_block(content, thinking);
        check(answer == content,
              "non-leading <think> is preserved verbatim (no regex-mangling)");
        check(thinking.empty(), "non-leading <think>: nothing captured as thinking");
    }

    // ------------------------------------------------------------------
    // 4. Streaming: content fed across chunk boundaries (partial closing tag).
    // ------------------------------------------------------------------
    {
        ThinkStreamSeparator sep;
        std::string answer;
        answer += sep.push_content("<thi");        // partial open tag
        answer += sep.push_content("nk>reason");   // completes open, inside think
        answer += sep.push_content("ing</thi");    // partial close tag held back
        answer += sep.push_content("nk>visible");  // completes close -> answer
        answer += sep.finish();
        check(answer == "visible",
              "streaming separator: only the answer is emitted across chunks");
        check(sep.thinking_text() == "reasoning",
              "streaming separator: reasoning accumulated across chunks");
    }

    // ------------------------------------------------------------------
    // 5. Structured message.thinking is accumulated off-band.
    // ------------------------------------------------------------------
    {
        ThinkStreamSeparator sep;
        sep.add_structured_thinking("step 1 ");
        sep.add_structured_thinking("step 2");
        const std::string answer = sep.push_content("plain answer") + sep.finish();
        check(answer == "plain answer",
              "structured thinking: answer untouched by structured channel");
        check(sep.thinking_text() == "step 1 step 2",
              "structured thinking: accumulated verbatim");
    }

    // ------------------------------------------------------------------
    // 6. show_thinking() reflects EMBER_SHOW_THINKING (default off).
    // ------------------------------------------------------------------
    {
        unsetenv("EMBER_SHOW_THINKING");
        check(!show_thinking(), "show_thinking: default OFF when unset");
        setenv("EMBER_SHOW_THINKING", "1", 1);
        check(show_thinking(), "show_thinking: '1' enables");
        setenv("EMBER_SHOW_THINKING", "true", 1);
        check(show_thinking(), "show_thinking: 'true' enables");
        setenv("EMBER_SHOW_THINKING", "off", 1);
        check(!show_thinking(), "show_thinking: 'off' stays off");
        setenv("EMBER_SHOW_THINKING", "0", 1);
        check(!show_thinking(), "show_thinking: '0' stays off");
        unsetenv("EMBER_SHOW_THINKING");
    }

    if (failures != 0) {
        std::cerr << failures << " thinking check(s) FAILED\n";
        return 1;
    }
    std::cout << "All thinking tests PASSED\n";
    return 0;
}
