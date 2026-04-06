#include "emberforge/telemetry/telemetry.hpp"

#include <iostream>

namespace emberforge::telemetry {

void ConsoleTelemetrySink::record(const Event& event) {
    std::cout << "[telemetry] " << event.name << ": " << event.details << '\n';
}

} // namespace emberforge::telemetry
