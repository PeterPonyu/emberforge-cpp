#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "emberforge/plugins/plugin.hpp"

namespace emberforge::plugins {

class PluginRegistry {
public:
    explicit PluginRegistry(std::vector<const Plugin*> plugins);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::vector<const Plugin*> list() const;
    [[nodiscard]] const Plugin* find_by_name(const std::string& name) const;

private:
    std::vector<const Plugin*> plugins_;
};

} // namespace emberforge::plugins
