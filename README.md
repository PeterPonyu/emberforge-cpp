# Emberforge (C++)

**Local-first terminal tooling for language-model workflows.**

Emberforge is a terminal coding tool that works with local models through Ollama and can use hosted providers when configured. This repository contains the C++ implementation, built with C++20, CMake, and a small dependency set.

## Quick Start

```bash
# Build from source
mkdir build && cd build && cmake .. && make

# Start the REPL (auto-detects Ollama)
./build/ember

# Or with a specific model
./build/ember --model qwen3:8b

# Run diagnostics
./build/ember doctor
```

## Features

- **Local-first**: Runs with Ollama — no API keys needed for local models
- **Hosted providers**: Anthropic Claude and xAI Grok when API keys are configured
- **Slash commands**: `/help`, `/status`, `/doctor`, `/model`, `/compact`, `/review`, `/commit`, `/pr`, and more
- **Tools**: bash, file ops, search, web, notebooks, agents, skills
- **Sessions**: Save, resume, export conversations
- **Plugin system**: Extend with custom tools and hooks
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
├── plugins/        Plugin system with pre/post tool hooks
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

Build:

```bash
mkdir build && cd build
cmake ..
make
```

The build produces a single `ember` binary and a `test_ollama_provider` test binary.

### Running tests

```bash
cd build
ctest --output-on-failure
# or directly:
./test_ollama_provider
```

## Model Support

| Provider | Models | Auth |
| --- | --- | --- |
| **Ollama** (local) | qwen3, llama3, gemma3, mistral, deepseek-r1, phi4, and more | None needed |
| **Anthropic** | Claude Opus 4.6, Sonnet 4.6, Haiku 4.5 | `ANTHROPIC_API_KEY` |
| **xAI** | Grok 3, Grok 3 Mini | `XAI_API_KEY` |

## Configuration

Emberforge reads configuration from (in order of priority):

1. `.ember.json` (project config)
2. `.ember/settings.json` (project settings)
3. `~/.ember/settings.json` (user settings)

Environment variables:

- `EMBER_CONFIG_HOME` — override config directory
- `OLLAMA_BASE_URL` — custom Ollama endpoint (default: `http://localhost:11434`)
- `EMBER_MODEL` — model to use with the Ollama provider (default: `qwen3:8b`)
- `ANTHROPIC_API_KEY` — Anthropic API credentials
- `XAI_API_KEY` — xAI API credentials

## License

MIT
