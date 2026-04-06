#include "emberforge/plugins/plugin.hpp"

namespace emberforge::plugins {

ExamplePlugin::ExamplePlugin()
    : metadata_{
          .id = "example.bundled",
          .name = "ExamplePlugin",
          .version = "0.1.0",
          .description = "A minimal plugin mirroring emberforge::plugins::Plugin",
      } {}

const PluginMetadata& ExamplePlugin::metadata() const {
    return metadata_;
}

bool ExamplePlugin::validate() const {
    return !metadata_.id.empty() && !metadata_.name.empty();
}

} // namespace emberforge::plugins
