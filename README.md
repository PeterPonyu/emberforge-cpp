# Emberforge (C++)

**A local-first coding forge for serious developers.**

Emberforge is an interactive coding assistant that runs in your terminal, powered by local LLMs via Ollama. This is the C++ implementation — built with modern C++20, CMake, and zero mandatory dependencies beyond a standard toolchain.

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
- **Cloud fallback**: Anthropic Claude, xAI Grok when API keys are configured
- **Rich slash commands**: `/help`, `/status`, `/doctor`, `/model`, `/compact`, `/review`, `/commit`, `/pr`, and more
- **Built-in tools**: bash, file ops, search, web, notebooks, agents, skills
- **Session persistence**: Save, resume, export conversations
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

Requires a C++20-capable compiler and CMake 3.20+.

```bash
mkdir build && cd build
cmake ..
make
```

The build produces a single `ember` binary.

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
- `OLLAMA_BASE_URL` — custom Ollama endpoint (default: `http://localhost:11434/v1`)
- `ANTHROPIC_API_KEY` — Anthropic API credentials
- `XAI_API_KEY` — xAI API credentials

## License

MIT
