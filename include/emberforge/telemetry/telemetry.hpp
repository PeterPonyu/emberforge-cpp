#pragma once

#include <string>

namespace emberforge::telemetry {

struct Event {
    std::string name;
    std::string details;
};

class TelemetrySink {
public:
    virtual ~TelemetrySink() = default;
    virtual void record(const Event& event) = 0;
};

class ConsoleTelemetrySink final : public TelemetrySink {
public:
    void record(const Event& event) override;
};

} // namespace emberforge::telemetry
