#include "emberforge/api/provider_router.hpp"

#include "emberforge/api/hosted_provider.hpp"
#include "emberforge/api/ollama_provider.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace emberforge::api {

namespace {

std::string read_env_non_empty(const char* key) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
    return value;
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool starts_with(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

// Returns true when the model name looks like a local Ollama tag. Mirrors the
// is_ollama_model heuristic in the Rust providers/mod.rs: colon-tagged names
// (e.g. "qwen3:8b") and a set of known local-model prefixes.
bool is_ollama_model(const std::string& lower) {
    if (lower.find(':') != std::string::npos) {
        return true;
    }
    static const char* kPrefixes[] = {
        "llama", "mistral", "phi", "gemma", "granite", "falcon",
        "solar", "starcoder", "internlm", "exaone", "qwen", "deepseek-r1",
    };
    for (const char* prefix : kPrefixes) {
        if (starts_with(lower, prefix)) {
            return true;
        }
    }
    return false;
}

} // namespace

const char* to_string(ProviderKind kind) {
    switch (kind) {
        case ProviderKind::Anthropic: return "anthropic";
        case ProviderKind::Xai:       return "xai";
        case ProviderKind::Ollama:    return "ollama";
    }
    return "unknown";
}

ProviderCredentials ProviderCredentials::from_env() {
    return ProviderCredentials{
        read_env_non_empty("ANTHROPIC_API_KEY"),
        read_env_non_empty("XAI_API_KEY"),
    };
}

ProviderKind resolve_provider_kind(const std::string& model,
                                   const ProviderCredentials& creds) {
    const std::string lower = to_lower(model);

    // 1. Model-name pattern wins first.
    if (!lower.empty()) {
        if (starts_with(lower, "grok")) {
            return ProviderKind::Xai;
        }
        if (starts_with(lower, "claude") || lower == "opus" ||
            lower == "sonnet" || lower == "haiku") {
            return ProviderKind::Anthropic;
        }
        if (is_ollama_model(lower)) {
            return ProviderKind::Ollama;
        }
    }

    // 2. Fall back to whichever hosted credential is present (Anthropic first).
    if (creds.has_anthropic()) {
        return ProviderKind::Anthropic;
    }
    if (creds.has_xai()) {
        return ProviderKind::Xai;
    }

    // 3. Ollama is the local default when no hosted credentials exist.
    return ProviderKind::Ollama;
}

std::unique_ptr<Provider> make_provider(const RouterConfig& config,
                                        const ProviderCredentials& creds) {
    const ProviderKind kind = resolve_provider_kind(config.model, creds);
    switch (kind) {
        case ProviderKind::Anthropic: {
            if (!creds.has_anthropic()) {
                throw std::runtime_error(
                    "make_provider: Anthropic selected but ANTHROPIC_API_KEY is missing");
            }
            const std::string base_url =
                config.anthropic_base_url.value_or(AnthropicProvider::kDefaultBaseUrl);
            return std::make_unique<AnthropicProvider>(creds.anthropic_api_key, config.model,
                                                       base_url);
        }
        case ProviderKind::Xai: {
            if (!creds.has_xai()) {
                throw std::runtime_error(
                    "make_provider: xAI selected but XAI_API_KEY is missing");
            }
            const std::string base_url =
                config.xai_base_url.value_or(xAiProvider::kDefaultBaseUrl);
            return std::make_unique<xAiProvider>(creds.xai_api_key, config.model, base_url);
        }
        case ProviderKind::Ollama:
            break;
    }
    const std::string model = config.model.empty() ? config.ollama_model : config.model;
    return std::make_unique<OllamaProvider>(config.ollama_base_url, model);
}

} // namespace emberforge::api
