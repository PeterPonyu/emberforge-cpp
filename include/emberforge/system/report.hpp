#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

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
    std::size_t handled_request_count;
    std::string lifecycle_state;
    std::optional<std::string> last_route;
    std::vector<std::string> last_phase_history;
    std::optional<std::string> last_turn_input;
};

} // namespace emberforge::system
