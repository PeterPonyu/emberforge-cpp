#pragma once

#include <string>

#include "emberforge/system/report.hpp"

namespace emberforge::system {

std::string build_doctor_report(const StarterSystemReport& report,
                                const std::string& base_url,
                                const std::string& model,
                                bool anthropic_api_key_present,
                                bool xai_api_key_present);

} // namespace emberforge::system
