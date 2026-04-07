#pragma once

#include <vector>

#include "emberforge/persistence/session_store.hpp"
#include "emberforge/system/application.hpp"
#include "emberforge/ui/command_dispatch.hpp"

namespace emberforge::ui {

class Repl {
public:
    explicit Repl(emberforge::system::StarterSystemApplication& app);

    int run();

private:
    emberforge::system::StarterSystemApplication& app_;
    CommandDispatch dispatch_;
    std::vector<emberforge::persistence::ConversationMessage> session_messages_;
};

} // namespace emberforge::ui
