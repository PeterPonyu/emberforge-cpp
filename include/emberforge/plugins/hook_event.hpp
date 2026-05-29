#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace emberforge::plugins {

// The 17 hook events defined by the cross-port contract (CROSS_PORT_CONTRACT.md
// §4.2). The wire names are PascalCase and MUST match the Rust reference
// (crates/plugins/src/hooks.rs + the contract's event table) byte-for-byte so
// that plugin manifests and HTTP payloads remain interchangeable across ports.
enum class HookEvent {
    PreToolUse,
    PostToolUse,
    SessionStart,
    SessionEnd,
    SubagentStart,
    SubagentStop,
    CompactStart,
    CompactEnd,
    ToolError,
    PermissionDenied,
    ConfigChange,
    UserPromptSubmit,
    Notification,
    PluginLoad,
    PluginUnload,
    CwdChanged,
    FileChanged,
};

// All 17 events, in contract order. Useful for iteration / parity assertions.
inline constexpr std::array<HookEvent, 17> kAllHookEvents{
    HookEvent::PreToolUse,    HookEvent::PostToolUse,      HookEvent::SessionStart,
    HookEvent::SessionEnd,    HookEvent::SubagentStart,    HookEvent::SubagentStop,
    HookEvent::CompactStart,  HookEvent::CompactEnd,       HookEvent::ToolError,
    HookEvent::PermissionDenied, HookEvent::ConfigChange,  HookEvent::UserPromptSubmit,
    HookEvent::Notification,  HookEvent::PluginLoad,       HookEvent::PluginUnload,
    HookEvent::CwdChanged,    HookEvent::FileChanged,
};

// Wire name for an event (PascalCase). Mirrors HookEvent::as_str in Rust.
[[nodiscard]] constexpr std::string_view to_wire_name(HookEvent event) {
    switch (event) {
        case HookEvent::PreToolUse:       return "PreToolUse";
        case HookEvent::PostToolUse:      return "PostToolUse";
        case HookEvent::SessionStart:     return "SessionStart";
        case HookEvent::SessionEnd:       return "SessionEnd";
        case HookEvent::SubagentStart:    return "SubagentStart";
        case HookEvent::SubagentStop:     return "SubagentStop";
        case HookEvent::CompactStart:     return "CompactStart";
        case HookEvent::CompactEnd:       return "CompactEnd";
        case HookEvent::ToolError:        return "ToolError";
        case HookEvent::PermissionDenied: return "PermissionDenied";
        case HookEvent::ConfigChange:     return "ConfigChange";
        case HookEvent::UserPromptSubmit: return "UserPromptSubmit";
        case HookEvent::Notification:     return "Notification";
        case HookEvent::PluginLoad:       return "PluginLoad";
        case HookEvent::PluginUnload:     return "PluginUnload";
        case HookEvent::CwdChanged:       return "CwdChanged";
        case HookEvent::FileChanged:      return "FileChanged";
    }
    return "Unknown";
}

// Parse a wire name back into a HookEvent. Returns nullopt for unrecognized
// names so callers can "gracefully skip unrecognized hook event types" as the
// contract conformance checklist (§11) requires.
[[nodiscard]] std::optional<HookEvent> hook_event_from_wire_name(std::string_view name);

// Whether this event carries tool context (tool_name / tool_input). Only tool
// events are subject to HookMatchRule filtering.
[[nodiscard]] constexpr bool is_tool_event(HookEvent event) {
    switch (event) {
        case HookEvent::PreToolUse:
        case HookEvent::PostToolUse:
        case HookEvent::ToolError:
            return true;
        default:
            return false;
    }
}

} // namespace emberforge::plugins
