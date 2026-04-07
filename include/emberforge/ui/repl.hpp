#pragma once

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
};

} // namespace emberforge::ui
