#include "emberforge/plugins/hook_event.hpp"

namespace emberforge::plugins {

std::optional<HookEvent> hook_event_from_wire_name(std::string_view name) {
    for (const HookEvent event : kAllHookEvents) {
        if (to_wire_name(event) == name) {
            return event;
        }
    }
    return std::nullopt;
}

} // namespace emberforge::plugins
