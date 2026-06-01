#pragma once

#include "emberforge/api/provider.hpp"

#include <memory>
#include <optional>
#include <string>

namespace emberforge::api {

// The hosted/local providers the router can resolve to. Mirrors the Rust
// ProviderKind (crates/api/src/providers/mod.rs), collapsed to the subset this
// C++ port implements.
enum class ProviderKind {
    Anthropic,
    Xai,
    Ollama,
};

const char* to_string(ProviderKind kind);

// Credentials resolved from the environment and/or explicit settings. Empty
// strings mean "absent". Settings take precedence over environment variables,
// mirroring the Rust resolution order (explicit config before env fallback).
struct ProviderCredentials {
    std::string anthropic_api_key;
    std::string xai_api_key;

    // Reads ANTHROPIC_API_KEY / XAI_API_KEY from the environment. Empty or
    // unset variables are treated as absent.
    static ProviderCredentials from_env();

    [[nodiscard]] bool has_anthropic() const { return !anthropic_api_key.empty(); }
    [[nodiscard]] bool has_xai() const { return !xai_api_key.empty(); }
};

// Configuration describing what to route. `model` selects the provider by name
// pattern (claude*/opus/sonnet/haiku -> Anthropic, grok* -> xAI, colon-tagged
// or otherwise -> Ollama). `ollama_base_url` / `ollama_model` parameterize the
// local default. Optional override base URLs let hosted providers target test
// servers.
struct RouterConfig {
    std::string model;
    std::string ollama_base_url = "http://localhost:11434";
    std::string ollama_model = "qwen3:8b";
    std::optional<std::string> anthropic_base_url;
    std::optional<std::string> xai_base_url;
};

// Resolves the provider kind for a given model name and available credentials,
// mirroring the Rust precedence:
//   1. model name pattern (grok* -> xAI, claude/opus/sonnet/haiku -> Anthropic,
//      colon-tagged local tags -> Ollama)
//   2. otherwise, available hosted credentials (Anthropic before xAI)
//   3. Ollama as the default when no hosted creds are present.
ProviderKind resolve_provider_kind(const std::string& model,
                                   const ProviderCredentials& creds);

// Builds a concrete Provider for the resolved kind. Throws std::runtime_error
// when a hosted provider is selected but its credential is missing.
std::unique_ptr<Provider> make_provider(const RouterConfig& config,
                                        const ProviderCredentials& creds);

} // namespace emberforge::api
