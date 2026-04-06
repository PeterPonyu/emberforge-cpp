#pragma once

#include <string>

namespace emberforge::plugins {

struct PluginMetadata {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
};

class Plugin {
public:
    virtual ~Plugin() = default;
    virtual const PluginMetadata& metadata() const = 0;
    virtual bool validate() const = 0;
};

class ExamplePlugin final : public Plugin {
public:
    ExamplePlugin();
    const PluginMetadata& metadata() const override;
    bool validate() const override;

private:
    PluginMetadata metadata_;
};

} // namespace emberforge::plugins
