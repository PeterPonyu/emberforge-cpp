// test_provider_router.cpp
//
// OFFLINE unit tests for the hosted provider routing layer (EFPORT-2).
// Exercises credential resolution, model->provider routing precedence, and
// request header/body construction WITHOUT any real network calls. No live
// API keys, no sockets — pure construction-level assertions.
//
// No external test framework — plain checks and a failure counter.

#include "emberforge/api/hosted_provider.hpp"
#include "emberforge/api/provider_router.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "PASS (" << name << ")\n";
    } else {
        std::cerr << "FAIL (" << name << ")\n";
        ++g_failures;
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    using namespace emberforge::api;

    // ------------------------------------------------------------------
    // Routing: model-name patterns take precedence over credentials.
    // ------------------------------------------------------------------
    {
        ProviderCredentials creds; // no hosted keys
        check(resolve_provider_kind("grok-3", creds) == ProviderKind::Xai,
              "grok_routes_to_xai");
        check(resolve_provider_kind("claude-sonnet-4-6", creds) == ProviderKind::Anthropic,
              "claude_routes_to_anthropic");
        check(resolve_provider_kind("opus", creds) == ProviderKind::Anthropic,
              "opus_alias_routes_to_anthropic");
        check(resolve_provider_kind("qwen3:8b", creds) == ProviderKind::Ollama,
              "colon_tag_routes_to_ollama");
        check(resolve_provider_kind("llama3.2", creds) == ProviderKind::Ollama,
              "ollama_prefix_routes_to_ollama");
    }

    // ------------------------------------------------------------------
    // Routing: with no model hint, fall back to credentials (Anthropic first).
    // ------------------------------------------------------------------
    {
        ProviderCredentials anth; anth.anthropic_api_key = "sk-ant-test";
        check(resolve_provider_kind("", anth) == ProviderKind::Anthropic,
              "anthropic_cred_fallback");

        ProviderCredentials xai; xai.xai_api_key = "xai-test";
        check(resolve_provider_kind("", xai) == ProviderKind::Xai,
              "xai_cred_fallback");

        ProviderCredentials both;
        both.anthropic_api_key = "sk-ant-test";
        both.xai_api_key = "xai-test";
        check(resolve_provider_kind("", both) == ProviderKind::Anthropic,
              "anthropic_wins_when_both_present");

        ProviderCredentials none;
        check(resolve_provider_kind("", none) == ProviderKind::Ollama,
              "ollama_default_when_no_creds");
    }

    // ------------------------------------------------------------------
    // Credential resolution from env (set/unset within the test process).
    // ------------------------------------------------------------------
    {
        setenv("ANTHROPIC_API_KEY", "sk-ant-env", 1);
        unsetenv("XAI_API_KEY");
        const auto creds = ProviderCredentials::from_env();
        check(creds.has_anthropic() && creds.anthropic_api_key == "sk-ant-env",
              "env_resolves_anthropic_key");
        check(!creds.has_xai(), "env_xai_absent");

        setenv("XAI_API_KEY", "", 1); // empty must be treated as absent
        const auto creds2 = ProviderCredentials::from_env();
        check(!creds2.has_xai(), "empty_env_key_is_absent");
        unsetenv("ANTHROPIC_API_KEY");
        unsetenv("XAI_API_KEY");
    }

    // ------------------------------------------------------------------
    // make_provider returns a working local provider with no creds and a
    // local model, and throws if a hosted kind is selected without its key.
    // ------------------------------------------------------------------
    {
        RouterConfig cfg;
        cfg.model = "qwen3:8b";
        ProviderCredentials none;
        auto provider = make_provider(cfg, none);
        check(provider != nullptr, "make_provider_ollama_default");

        bool threw = false;
        try {
            RouterConfig grok_cfg;
            grok_cfg.model = "grok-3";
            make_provider(grok_cfg, none); // xAI selected, no key
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "make_provider_throws_without_hosted_key");
    }

    // ------------------------------------------------------------------
    // AnthropicProvider header + body construction (offline).
    // ------------------------------------------------------------------
    {
        AnthropicProvider provider("sk-ant-123", "claude-sonnet-4-6",
                                   "https://anthropic.test");
        const auto headers = provider.build_headers();

        bool has_api_key = false, has_version = false, has_caching_beta = false;
        for (const auto& h : headers) {
            if (contains(h, "x-api-key: sk-ant-123")) has_api_key = true;
            if (contains(h, "anthropic-version: 2023-06-01")) has_version = true;
            if (contains(h, "anthropic-beta: prompt-caching-2024-07-31")) has_caching_beta = true;
        }
        check(has_api_key, "anthropic_sends_x_api_key");
        check(has_version, "anthropic_sends_version_header");
        check(has_caching_beta, "anthropic_sends_prompt_caching_beta");
        check(provider.endpoint() == "https://anthropic.test/v1/messages",
              "anthropic_endpoint");

        const std::string body =
            provider.build_body(MessageRequest{"", "hello there"}, "you are helpful");
        check(contains(body, "\"model\":\"claude-sonnet-4-6\""), "anthropic_body_model");
        check(contains(body, "hello there"), "anthropic_body_prompt");
        // Prompt caching marker on the system block.
        check(contains(body, "cache_control") && contains(body, "ephemeral"),
              "anthropic_body_cache_control");
        check(contains(body, "you are helpful"), "anthropic_body_system");

        // No system prompt -> no system block emitted.
        const std::string bare = provider.build_body(MessageRequest{"", "hi"});
        check(!contains(bare, "cache_control"), "anthropic_no_system_no_cache_control");
    }

    // ------------------------------------------------------------------
    // xAiProvider header + body construction (offline).
    // ------------------------------------------------------------------
    {
        xAiProvider provider("xai-456", "grok-3", "https://x.ai.test/v1");
        const auto headers = provider.build_headers();

        bool has_bearer = false;
        for (const auto& h : headers) {
            if (contains(h, "Authorization: Bearer xai-456")) has_bearer = true;
        }
        check(has_bearer, "xai_sends_bearer_auth");
        check(provider.endpoint() == "https://x.ai.test/v1/chat/completions",
              "xai_endpoint");

        const std::string body =
            provider.build_body(MessageRequest{"", "weather?"}, "be terse");
        check(contains(body, "\"model\":\"grok-3\""), "xai_body_model");
        check(contains(body, "\"role\":\"system\"") && contains(body, "be terse"),
              "xai_body_system_message");
        check(contains(body, "\"role\":\"user\"") && contains(body, "weather?"),
              "xai_body_user_message");

        // model override from the request takes precedence over constructor model.
        const std::string overridden =
            provider.build_body(MessageRequest{"grok-2", "hi"});
        check(contains(overridden, "\"model\":\"grok-2\""), "xai_body_model_override");
    }

    if (g_failures == 0) {
        std::cout << "All provider router tests PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " provider router test(s) FAILED\n";
    return 1;
}
