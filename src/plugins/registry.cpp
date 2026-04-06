#include "emberforge/plugins/registry.hpp"

#include <algorithm>

namespace emberforge::plugins {

PluginRegistry::PluginRegistry(std::vector<const Plugin*> plugins)
    : plugins_(std::move(plugins)) {}

std::size_t PluginRegistry::size() const {
    return plugins_.size();
}

std::vector<const Plugin*> PluginRegistry::list() const {
    return plugins_;
}

const Plugin* PluginRegistry::find_by_name(const std::string& name) const {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(), [&name](const Plugin* plugin) {
        return plugin != nullptr && plugin->metadata().name == name;
    });
    return it == plugins_.end() ? nullptr : *it;
}

} // namespace emberforge::plugins
