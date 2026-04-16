#include "emberforge/system/doctor.hpp"

#include <sstream>

namespace emberforge::system {

std::string build_doctor_report(const StarterSystemReport& report,
                                const std::string& base_url,
                                const std::string& model,
                                bool anthropic_api_key_present,
                                bool xai_api_key_present) {
    std::ostringstream out;
    out << "emberforge-cpp doctor\n";
    out << "provider: ollama\n";
    out << "base_url: " << base_url << '\n';
    out << "model: " << model << '\n';
    out << "anthropic_api_key: " << (anthropic_api_key_present ? "present" : "missing") << '\n';
    out << "xai_api_key: " << (xai_api_key_present ? "present" : "missing") << '\n';
    out << "commands: " << report.command_count << '\n';
    out << "tools: " << report.tool_count << '\n';
    out << "plugins: " << report.plugin_count << '\n';
    out << "server: " << report.server_description << '\n';
    out << "lsp: " << report.lsp_summary << '\n';
    out << "lifecycle: " << report.lifecycle_state;
    return out.str();
}

} // namespace emberforge::system
