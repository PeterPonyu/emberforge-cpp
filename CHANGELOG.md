# Changelog

All notable changes to Emberforge (C++) are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- `prompt` subcommand: non-interactive one-shot agent turn (direct-loop parity with Rust reference)
- Hosted providers: Anthropic Claude and xAI Grok via `ANTHROPIC_API_KEY` / `XAI_API_KEY`
- Real tool executor: `bash`, `read_file`, `write_file`, `edit_file`, `glob_search`, `grep_search`
- Multi-turn agentic tool loop with native Ollama tool-calling
- Streaming NDJSON response handling via libcurl
- Thinking-model support: structured `message.thinking` channel + inline `<think>` block stripping for qwen3 / deepseek-r1
- `OllamaProvider::normalize_base_url`: accepts both root (`http://HOST:PORT`) and OpenAI-compat (`/v1`) forms
- JSONL telemetry and session persistence
- LSP manager integration
- CTest-driven unit and integration test suite
- `doctor` subcommand for provider connectivity diagnostics
- AGENTS.md operating contract for AI agents working in this repository
- LICENSE (MIT), CONTRIBUTING.md

### Changed
- `EMBER_OLLAMA_NUM_PREDICT` default is now model-aware: `32000` for opus models, `64000` otherwise (mirrors Rust reference `max_tokens_for_model`)

### Not yet ported from Rust reference
- `--output-format json|ndjson` structured output flag
- `--permission-mode` and `--allowed-tools` CLI flags
- Decision ledger
- MCP client
- Plugin runtime hooks (metadata + registry scaffolding present only)
