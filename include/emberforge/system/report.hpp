#pragma once

#include <optional>
#include <string>

namespace emberforge::system {

struct StarterSystemReport {
    std::string app_name;
    std::size_t command_count;
    std::size_t tool_count;
    std::size_t plugin_count;
    std::string server_description;
    std::string lsp_summary;
    std::string rust_anchor;
    std::size_t turn_count;
    std::optional<std::string> last_turn_input;
};

} // namespace emberforge::system
