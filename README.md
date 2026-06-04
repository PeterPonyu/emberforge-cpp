# Emberforge (C++)

**Local-first terminal tooling for language-model workflows.**

Emberforge is a terminal coding tool that works with local models through Ollama and can use hosted providers when configured. This repository contains the C++ implementation, built with C++20, CMake, and a small dependency set.

## Quick Start

```bash
# Build from source (out-of-source, generator-agnostic)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# The ember binary is produced in the build/ directory (no install step;
# run it in place as ./build/ember).

# One-shot direct loop: run a single non-interactive agent turn and exit
./build/ember prompt "summarize this repository"

# Start the REPL (auto-detects Ollama)
./build/ember

# Or with a specific model
./build/ember --model qwen3:8b prompt "explain the provider router"

# Run diagnostics
./build/ember doctor
```

> **Agents/automation:** see [AGENTS.md](AGENTS.md) for the full build/run/test contract.

### Direct loop (`prompt`)

The `prompt` subcommand runs **one** turn through the conversation runtime, prints the
result to stdout, and exits — the headless path for scripts and automation:

```bash
./build/ember prompt "<your prompt text>"
./build/ember --model qwen3:8b prompt "<your prompt text>"
```

Output is plain text (there is no JSON output mode in the C++ port). The one-shot path
needs a reachable provider — with the default Ollama backend, start `ollama serve` first
or set a hosted provider key (see below).

## Features

- **Local-first**: Runs with Ollama — no API keys needed for local models
- **Hosted providers**: Anthropic Claude and xAI Grok when API keys are configured
- **Slash commands**: `/help`, `/status`, `/doctor`, `/model`, `/questions`, `/tasks`, `/buddy`, `/compact`, `/review`, `/commit`, `/pr`, and more
- **Tools**: bash, file ops, search, web, notebooks, agents, skills
- **Sessions**: Save, resume, export conversations
- **Plugin system**: Includes plugin metadata and registry scaffolding
- **MCP integration**: Connect to Model Context Protocol servers
- **Telemetry**: Session tracing and usage analytics
- **LSP support**: Language Server Protocol integration

## Architecture

```text
include/emberforge/
├── api/            Provider routing — Ollama, Anthropic, xAI
├── commands/       Slash command definitions and help text
├── compat/         Compatibility / upstream path resolution
├── lsp/            Language Server Protocol integration
├── plugins/        Plugin metadata and registry surfaces; runtime hook parity is planned
├── runtime/        Session state, config, model profiles
├── server/         HTTP/SSE server infrastructure
├── system/         Application lifecycle, config, dispatch, control sequences
├── telemetry/      Session tracing, analytics events
└── tools/          Built-in tool specs with execution dispatch

src/                Implementation files mirroring the include layout
apps/ember_cli/     CLI entry point
```

## Building

Requires a C++20-capable compiler, CMake 3.20+, **libcurl** (for the Ollama HTTP provider), and **nlohmann/json** (for NDJSON parsing).

Install dependencies on Debian/Ubuntu:

```bash
sudo apt-get install libcurl4-openssl-dev nlohmann-json3-dev
```

**JSON library**: nlohmann/json (system package `nlohmann-json3-dev`, v3.11.3+) is used for NDJSON parsing in `OllamaProvider`. It correctly handles all JSON escape sequences including `\uXXXX` Unicode escapes. The system package is preferred over vendoring to avoid tracking large generated headers in source control.

Build (matches the CI workflow):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The build produces the `ember` binary plus several test binaries (run via
`ctest`): `test_ollama_provider`, `test_real_tool_executor`,
`test_command_dispatch`, `test_session_store`, `test_doctor`,
`test_hook_engine`, `test_upstream_paths`, `test_telemetry`,
`test_lsp_manager`, `test_provider_router`, `test_tool_registry`, and
`test_prompt_command` (the `prompt` direct-loop subcommand).

### Running tests

```bash
ctest --test-dir build --output-on-failure
```

## Model Support

| Provider | Models | Auth |
| --- | --- | --- |
| **Ollama** (local) | qwen3, llama3, gemma3, mistral, deepseek-r1, phi4, and more | None needed |
| **Anthropic** | Claude Opus, Sonnet, and Haiku families | `ANTHROPIC_API_KEY` |
| **xAI** | Grok 3, Grok 3 Mini | `XAI_API_KEY` |

The provider router selects a backend by **model name first, then credentials**: `grok*`
routes to xAI; `claude*` / `opus` / `sonnet` / `haiku` route to Anthropic; known Ollama
models route to Ollama. If the model name is ambiguous, the first hosted credential present
wins (Anthropic, then xAI); otherwise Ollama is the local default. If a hosted provider is
selected but its key is missing, startup fails with a clear error.

## Configuration

Emberforge reads configuration from (in order of priority):

1. `.ember.json` (project config)
2. `.ember/settings.json` (project settings)
3. `~/.ember/settings.json` (user settings)

Environment variables:

- `EMBER_CONFIG_HOME` — override config directory
- `OLLAMA_BASE_URL` — custom Ollama endpoint (default: `http://localhost:11434`)
- `EMBER_MODEL` — model to use with the Ollama provider (default: `qwen3:8b`)
- `EMBER_OLLAMA_NUM_PREDICT` — output-token bound (`num_predict`) for Ollama
  chat requests, capping runaway generation by thinking models (e.g. qwen3).
  Default is model-aware, mirroring the Rust reference's `max_tokens_for_model`:
  `32000` for `opus` models, `64000` otherwise. Set a positive integer to override.
- `ANTHROPIC_API_KEY` — Anthropic API credentials
- `XAI_API_KEY` — xAI API credentials

## License

MIT
