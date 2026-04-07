#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace emberforge::system {
class StarterSystemApplication;
} // namespace emberforge::system

namespace emberforge::ui {

using CommandHandler = std::function<int(emberforge::system::StarterSystemApplication&,
                                         const std::vector<std::string>&)>;

class CommandDispatch {
public:
    CommandDispatch();

    void register_handler(std::string name, CommandHandler handler);

    // Returns 0 on success, 1 on unknown command, or the handler's return value.
    int invoke(std::string name,
               emberforge::system::StarterSystemApplication& app,
               std::vector<std::string> args);

    // Read-only view of registered command names (for /help).
    const std::map<std::string, CommandHandler>& handlers() const { return handlers_; }

private:
    std::map<std::string, CommandHandler> handlers_;
};

} // namespace emberforge::ui
