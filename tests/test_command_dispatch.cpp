#include "emberforge/ui/command_dispatch.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "emberforge/api/provider.hpp"
#include "emberforge/system/application.hpp"

int main() {
    // Build a real application so we have something to pass to invoke().
    auto provider = std::make_unique<emberforge::api::MockProvider>();
    emberforge::system::StarterSystemApplication app(std::move(provider));

    // ------------------------------------------------------------------
    // Test 1: help_lists_registered_commands
    // Register 2 extra handlers, call /help (via Repl-style iteration),
    // and assert both appear in the map.
    // ------------------------------------------------------------------
    {
        emberforge::ui::CommandDispatch dispatch;

        bool alpha_called = false;
        bool beta_called  = false;

        dispatch.register_handler("alpha", [&](emberforge::system::StarterSystemApplication&,
                                               const std::vector<std::string>&) -> int {
            alpha_called = true;
            return 0;
        });
        dispatch.register_handler("beta", [&](emberforge::system::StarterSystemApplication&,
                                              const std::vector<std::string>&) -> int {
            beta_called = true;
            return 0;
        });

        // Capture stdout while printing /help output.
        std::streambuf* old_buf = std::cout.rdbuf();
        std::ostringstream oss;
        std::cout.rdbuf(oss.rdbuf());

        // Simulate what Repl::run() does for /help.
        for (const auto& [name, _] : dispatch.handlers()) {
            std::cout << "  /" << name << '\n';
        }

        std::cout.rdbuf(old_buf);
        const std::string output = oss.str();

        if (output.find("/alpha") == std::string::npos) {
            std::cerr << "FAIL (help_lists_registered_commands): /alpha not in output\n";
            std::cerr << "Output was:\n" << output << '\n';
            return 1;
        }
        if (output.find("/beta") == std::string::npos) {
            std::cerr << "FAIL (help_lists_registered_commands): /beta not in output\n";
            return 1;
        }

        // Confirm the handlers themselves still work.
        dispatch.invoke("alpha", app, {});
        dispatch.invoke("beta",  app, {});
        assert(alpha_called && "alpha handler should have been called");
        assert(beta_called  && "beta handler should have been called");

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

    std::cout << "All CommandDispatch tests PASSED\n";
    return 0;
}
