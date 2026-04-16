#include "emberforge/ui/command_dispatch.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "emberforge/api/provider.hpp"
#include "emberforge/system/application.hpp"

int main() {
    const auto buddy_state_path = std::filesystem::temp_directory_path() / "emberforge-buddy-command-dispatch.json";
    const auto task_state_path = std::filesystem::temp_directory_path() / "emberforge-task-command-dispatch.json";
    std::filesystem::remove(buddy_state_path);
    std::filesystem::remove(task_state_path);
    setenv("EMBER_BUDDY_STATE_PATH", buddy_state_path.string().c_str(), 1);
    setenv("EMBER_TASK_STATE_PATH", task_state_path.string().c_str(), 1);

    // Build a real application so we have something to pass to invoke().
    auto provider = std::make_unique<emberforge::api::MockProvider>();
    emberforge::system::StarterSystemApplication app(std::move(provider));

    // ------------------------------------------------------------------
    // Test 1: help_lists_registered_commands
    // Call /help and assert starter commands plus hints render.
    // ------------------------------------------------------------------
    {
        emberforge::ui::CommandDispatch dispatch;

        // Capture stdout while printing /help output.
        std::streambuf* old_buf = std::cout.rdbuf();
        std::ostringstream oss;
        std::cout.rdbuf(oss.rdbuf());

        const int rc = dispatch.invoke("help", app, {});

        std::cout.rdbuf(old_buf);
        const std::string output = oss.str();
        if (rc != 0) {
            std::cerr << "FAIL (help_lists_registered_commands): expected rc=0, got " << rc << '\n';
            return 1;
        }

        if (output.find("/doctor [quick|status]") == std::string::npos) {
            std::cerr << "FAIL (help_lists_registered_commands): /doctor hint not in output\n";
            std::cerr << "Output was:\n" << output << '\n';
            return 1;
        }
        if (output.find("/questions [ask <task-id> <text>|pending|answer <question-id> <text>]") == std::string::npos) {
            std::cerr << "FAIL (help_lists_registered_commands): /questions hint not in output\n";
            return 1;
        }
        if (output.find("/tasks [create prompt <text>|list|show <task-id>|stop <task-id>]") == std::string::npos) {
            std::cerr << "FAIL (help_lists_registered_commands): /tasks hint not in output\n";
            return 1;
        }
        if (output.find("/buddy [hatch|rehatch|pet|mute|unmute]") == std::string::npos) {
            std::cerr << "FAIL (help_lists_registered_commands): /buddy hint not in output\n";
            return 1;
        }
        if (output.find("/pr [context]") == std::string::npos) {
            std::cerr << "FAIL (help_lists_registered_commands): /pr hint not in output\n";
            return 1;
        }

        std::cout << "PASS (help_lists_registered_commands)\n";
    }

    // ------------------------------------------------------------------
    // Test 2: unknown_command_returns_error
    // ------------------------------------------------------------------
    {
        emberforge::ui::CommandDispatch dispatch;

        // Redirect stderr to swallow the "unknown command" message.
        std::streambuf* old_err = std::cerr.rdbuf();
        std::ostringstream err_oss;
        std::cerr.rdbuf(err_oss.rdbuf());

        const int rc = dispatch.invoke("nope", app, {});

        std::cerr.rdbuf(old_err);

        if (rc == 0) {
            std::cerr << "FAIL (unknown_command_returns_error): expected non-zero, got 0\n";
            return 1;
        }
        std::cout << "PASS (unknown_command_returns_error): rc=" << rc << '\n';
    }

    // ------------------------------------------------------------------
    // Test 3: registered_handler_receives_args
    // ------------------------------------------------------------------
    {
        emberforge::ui::CommandDispatch dispatch;

        std::vector<std::string> captured_args;
        dispatch.register_handler("mytest", [&](emberforge::system::StarterSystemApplication&,
                                                const std::vector<std::string>& args) -> int {
            captured_args = args;
            return 0;
        });

        const std::vector<std::string> sent_args{"foo", "bar", "baz"};
        const int rc = dispatch.invoke("mytest", app, sent_args);
        assert(rc == 0 && "handler should return 0");
        assert(captured_args == sent_args && "handler should receive exactly the passed args");

        std::cout << "PASS (registered_handler_receives_args)\n";
    }

    // ------------------------------------------------------------------
    // Test 4: starter_slash_commands_are_callable
    // ------------------------------------------------------------------
    {
        emberforge::ui::CommandDispatch dispatch;

        const std::vector<std::string> commands{
            "doctor",
            "questions",
            "tasks",
            "buddy",
            "compact",
            "review",
            "commit",
            "pr",
        };

        for (const auto& name : commands) {
            const int rc = dispatch.invoke(name, app, {});
            if (rc != 0) {
                std::cerr << "FAIL (starter_slash_commands_are_callable): /" << name
                          << " returned " << rc << '\n';
                return 1;
            }
        }

        std::cout << "PASS (starter_slash_commands_are_callable)\n";
    }

    // ------------------------------------------------------------------
    // Test 5: starter_commands_accept_payload_args
    // ------------------------------------------------------------------
    {
        emberforge::ui::CommandDispatch dispatch;

        std::streambuf* old_buf = std::cout.rdbuf();
        std::ostringstream oss;
        std::cout.rdbuf(oss.rdbuf());

        const int task_create_rc = dispatch.invoke("tasks", app, {"create", "prompt", "investigate", "auth"});
        const int question_ask_rc = dispatch.invoke("questions", app, {"ask", "task-1", "Which", "tenant?"});
        const int model_rc = dispatch.invoke("model", app, {"list"});
        const int buddy_rc = dispatch.invoke("buddy", app, {"hatch"});
        const int review_rc = dispatch.invoke("review", app, {"workspace"});
        const int pr_rc = dispatch.invoke("pr", app, {"release", "notes"});

        std::cout.rdbuf(old_buf);
        const std::string output = oss.str();

        if (task_create_rc != 0 || question_ask_rc != 0 || model_rc != 0 || buddy_rc != 0 || review_rc != 0 || pr_rc != 0) {
            std::cerr << "FAIL (starter_commands_accept_payload_args): unexpected non-zero rc\n";
            return 1;
        }
        if (output.find("[command] tasks create") == std::string::npos || output.find("task_id: task-1") == std::string::npos) {
            std::cerr << "FAIL (starter_commands_accept_payload_args): task create output missing\n";
            return 1;
        }
        if (output.find("[command] questions ask") == std::string::npos || output.find("question_id: question-1") == std::string::npos) {
            std::cerr << "FAIL (starter_commands_accept_payload_args): questions ask output missing\n";
            return 1;
        }
        if (output.find("model list:") == std::string::npos) {
            std::cerr << "FAIL (starter_commands_accept_payload_args): model list output missing\n";
            return 1;
        }
        if (output.find("[command] buddy hatch") == std::string::npos ||
            output.find("name: Waddles") == std::string::npos) {
            std::cerr << "FAIL (starter_commands_accept_payload_args): buddy payload missing\n";
            return 1;
        }
        if (output.find("[command] review") == std::string::npos || output.find("scope: workspace") == std::string::npos) {
            std::cerr << "FAIL (starter_commands_accept_payload_args): review payload missing\n";
            return 1;
        }
        if (output.find("[command] pr") == std::string::npos || output.find("context: release notes") == std::string::npos) {
            std::cerr << "FAIL (starter_commands_accept_payload_args): pr payload missing\n";
            return 1;
        }

        std::cout << "PASS (starter_commands_accept_payload_args)\n";
    }

    // ------------------------------------------------------------------
    // Test 6: buddy_state_persists_across_app_instances
    // ------------------------------------------------------------------
    {
        auto provider2 = std::make_unique<emberforge::api::MockProvider>();
        emberforge::system::StarterSystemApplication app2(std::move(provider2));
        emberforge::ui::CommandDispatch dispatch;

        std::streambuf* old_buf = std::cout.rdbuf();
        std::ostringstream oss;
        std::cout.rdbuf(oss.rdbuf());

        const int rc = dispatch.invoke("buddy", app2, {});

        std::cout.rdbuf(old_buf);
        const std::string output = oss.str();

        if (rc != 0) {
            std::cerr << "FAIL (buddy_state_persists_across_app_instances): rc=" << rc << '\n';
            return 1;
        }
        if (output.find("name: Waddles") == std::string::npos) {
            std::cerr << "FAIL (buddy_state_persists_across_app_instances): persisted buddy missing\n";
            return 1;
        }

        std::streambuf* old_buf_rehatch = std::cout.rdbuf();
        std::ostringstream rehatch_oss;
        std::cout.rdbuf(rehatch_oss.rdbuf());

        const int rehatch_rc = dispatch.invoke("buddy", app2, {"rehatch"});

        std::cout.rdbuf(old_buf_rehatch);
        const std::string rehatch_output = rehatch_oss.str();

        if (rehatch_rc != 0 || rehatch_output.find("name: Goosberry") == std::string::npos) {
            std::cerr << "FAIL (buddy_state_persists_across_app_instances): rehatch did not advance\n";
            return 1;
        }

        std::cout << "PASS (buddy_state_persists_across_app_instances)\n";
    }

    // ------------------------------------------------------------------
    // Test 7: task_question_resume_flow_persists_across_app_instances
    // ------------------------------------------------------------------
    {
        std::filesystem::remove(task_state_path);
        setenv("EMBER_TASK_STATE_PATH", task_state_path.string().c_str(), 1);

        auto provider2 = std::make_unique<emberforge::api::MockProvider>();
        emberforge::system::StarterSystemApplication app2(std::move(provider2));
        emberforge::ui::CommandDispatch dispatch;

        {
            std::streambuf* old_buf = std::cout.rdbuf();
            std::ostringstream oss;
            std::cout.rdbuf(oss.rdbuf());
            dispatch.invoke("tasks", app2, {"create", "prompt", "investigate", "auth", "flow"});
            dispatch.invoke("questions", app2, {"ask", "task-1", "Which", "tenant", "first?"});
            std::cout.rdbuf(old_buf);
        }

        auto provider3 = std::make_unique<emberforge::api::MockProvider>();
        emberforge::system::StarterSystemApplication app3(std::move(provider3));

        std::streambuf* old_buf = std::cout.rdbuf();
        std::ostringstream oss;
        std::cout.rdbuf(oss.rdbuf());

        const int pending_rc = dispatch.invoke("questions", app3, {});
        const int show_waiting_rc = dispatch.invoke("tasks", app3, {"show", "task-1"});
        const int answer_rc = dispatch.invoke("questions", app3, {"answer", "question-1", "Start", "with", "billing"});
        const int show_completed_rc = dispatch.invoke("tasks", app3, {"show", "task-1"});

        std::cout.rdbuf(old_buf);
        const std::string output = oss.str();

        if (pending_rc != 0 || show_waiting_rc != 0 || answer_rc != 0 || show_completed_rc != 0) {
            std::cerr << "FAIL (task_question_resume_flow_persists_across_app_instances): unexpected non-zero rc\n";
            return 1;
        }
        if (output.find("question-1 -> task-1") == std::string::npos) {
            std::cerr << "FAIL (task_question_resume_flow_persists_across_app_instances): pending question missing\n";
            return 1;
        }
        if (output.find("status: waiting_for_user") == std::string::npos) {
            std::cerr << "FAIL (task_question_resume_flow_persists_across_app_instances): waiting state missing\n";
            return 1;
        }
        if (output.find("task_status: completed") == std::string::npos) {
            std::cerr << "FAIL (task_question_resume_flow_persists_across_app_instances): completion after answer missing\n";
            return 1;
        }
        if (output.find("answer: Start with billing") == std::string::npos) {
            std::cerr << "FAIL (task_question_resume_flow_persists_across_app_instances): answer missing\n";
            return 1;
        }

        const auto transcript_path = task_state_path.parent_path() / "task-question-transcript.jsonl";
        std::ifstream transcript(transcript_path);
        if (!transcript) {
            std::cerr << "FAIL (task_question_resume_flow_persists_across_app_instances): transcript missing\n";
            return 1;
        }
        std::stringstream transcript_buffer;
        transcript_buffer << transcript.rdbuf();
        const auto transcript_text = transcript_buffer.str();
        if (transcript_text.find("\"id\":\"task-question-runtime\"") == std::string::npos ||
            transcript_text.find("\"type\":\"task_state\"") == std::string::npos ||
            transcript_text.find("\"type\":\"question_state\"") == std::string::npos ||
            transcript_text.find("\"status\":\"waiting_for_user\"") == std::string::npos ||
            transcript_text.find("\"status\":\"completed\"") == std::string::npos) {
            std::cerr << "FAIL (task_question_resume_flow_persists_across_app_instances): transcript content missing\n";
            return 1;
        }

        std::cout << "PASS (task_question_resume_flow_persists_across_app_instances)\n";
    }

    std::cout << "All CommandDispatch tests PASSED\n";
    return 0;
}
