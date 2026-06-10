# AGENTS.md — Emberforge (C++)

Operating contract for AI agents and automation working in this repository. Everything
below is verified against the C++ port as it actually builds and runs. Where a feature is
partial or absent, it is called out plainly.

## What this is

Emberforge (C++) is a local-first terminal coding tool built with C++20, CMake, libcurl,
and nlohmann/json. It talks to local models via Ollama by default and can route to hosted
providers (Anthropic, xAI) when API keys are present.

There is **no install target** — the build produces a `build/ember` binary that you run in
place (`./build/ember`). There is no system-wide `ember` on `PATH` unless you add one.

## Build

System dependencies (Debian/Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y cmake libcurl4-openssl-dev nlohmann-json3-dev
```

Requires a C++20 compiler and CMake 3.20+.

Configure and build (identical to `.github/workflows/ci.yml`):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

This produces `build/ember` plus the test binaries.

## Direct loop (non-interactive one-shot)

Run a single agent turn and exit — the headless path for scripting and automation:

```bash
./build/ember prompt "summarize the architecture of this repo"
```

The `prompt` subcommand joins its argument(s) into one prompt string, runs **exactly one**
turn through the existing `ConversationRuntime` (`StarterSystemApplication::run_prompt` →
`ControlSequenceEngine::handle` → `ConversationRuntime::run_turn`), prints the result to
stdout, and exits. This mirrors the Rust reference (`CliAction::Prompt` →
`run_turn_with_output`).

Notes / current limitations:

- Output is **plain text only**. There is no `--output json` flag in the C++ port (only the
  Rust reference exposes output-format selection).
- Pick the model with `--model <name>` (or the `EMBER_MODEL` env var):
  `./build/ember --model qwen3:8b prompt "hello"`.
- A leading non-flag token that is not a known subcommand (`doctor`, `serve`) and does not
  start with `/` is also treated as prompt text, so `./build/ember "hello"` works too. Use
  the explicit `prompt` form in scripts for clarity.
- With no prompt text, `./build/ember prompt` prints `prompt: no text provided` and exits 1.
- The one-shot path needs a reachable provider. With the default Ollama provider and no
  server running you get `Ollama error: ... Couldn't connect to server` (exit 1).

Other CLI entry points: `./build/ember doctor` (diagnostics), `./build/ember doctor status`,
`./build/ember --repl` (interactive REPL), `./build/ember /help` (list slash commands).

## Providers and environment variables

The provider router (`src/api/provider_router.cpp`) selects a backend by **model name first,
then available credentials**:

1. Model-name pattern: `grok*` → xAI, `claude*` / `opus` / `sonnet` / `haiku` → Anthropic,
   known Ollama models → Ollama.
2. Otherwise, the first hosted credential present (Anthropic, then xAI).
3. Otherwise, Ollama (the local default).

| Provider | Default? | Auth env var | Notes |
| --- | --- | --- | --- |
| **Ollama** (local) | Yes | none | Set `OLLAMA_BASE_URL` (default `http://localhost:11434`; a trailing `/v1` is stripped automatically) |
| **Anthropic** | No | `ANTHROPIC_API_KEY` | Claude Opus / Sonnet / Haiku families |
| **xAI** | No | `XAI_API_KEY` | Grok models |

Relevant environment variables:

- `OLLAMA_BASE_URL` — Ollama endpoint (default `http://localhost:11434`). Both the root form
  (`http://HOST:PORT`) and the OpenAI-compatible form (`http://HOST:PORT/v1`) are accepted;
  `OllamaProvider::normalize_base_url` strips a trailing `/v1` before appending `/api/chat`.
- `EMBER_MODEL` — model for the Ollama provider (default `qwen3:8b`)
- `ANTHROPIC_API_KEY` — selects/enables the Anthropic provider
- `XAI_API_KEY` — selects/enables the xAI provider
- `EMBER_CONFIG_HOME` — override config directory

If a hosted provider is selected by model name but its key is missing, `make_provider`
throws (e.g. `Anthropic selected but ANTHROPIC_API_KEY is missing`).

## Tests

The full suite is registered with CTest and run exactly as CI does:

```bash
ctest --test-dir build --output-on-failure
```

Test sources live in `tests/` (e.g. `test_command_dispatch.cpp`, `test_provider_router.cpp`,
`test_prompt_command.cpp`). The `prompt` direct-loop test uses `MockProvider` for the
one-shot turn assertions and invokes the built `ember` binary for the subcommand
argument-handling path, so it needs no network or Ollama server.

When adding a test, add an `add_executable` + `add_test` pair in `CMakeLists.txt` mirroring
the existing entries (link against `emberforge_core`, include `include/`).

## Repository layout

```text
apps/ember_cli/main.cpp   CLI entry point (arg parsing, doctor/prompt/repl/demo dispatch)
include/emberforge/        Public headers
  api/                     Provider routing — Ollama, Anthropic, xAI
  commands/                Slash-command definitions and help text
  compat/                  Compatibility / upstream path resolution
  lsp/                     Language Server Protocol integration
  plugins/                 Plugin metadata and registry surfaces
  runtime/                 Session state, conversation runtime (run_turn)
  server/                  HTTP/SSE server infrastructure
  system/                  Application lifecycle, dispatch, control sequence, doctor
  telemetry/               Session tracing, analytics events
  tools/                   Built-in tool specs with execution dispatch
src/                       Implementation files mirroring the include/ layout
tests/                     CTest-driven unit/integration tests
CMakeLists.txt             Build + test registration
.github/workflows/ci.yml   CI: apt deps → configure → build → ctest
```

## Parity gaps vs Rust reference

The following Rust-reference features are **not implemented** in the C++ port.
Do not add code for them unless the task explicitly requests it.

| Feature | Rust flag / behaviour | C++ status |
| --- | --- | --- |
| Structured output from `prompt` | `--output-format json\|ndjson` | Not implemented — plain text only |
| Permission mode flag | `--permission-mode read-only\|workspace-write\|danger-full-access` | Not implemented — no flag; env var `EMBER_PERMISSION_MODE` not yet wired |
| Allowed-tools flag | `--allowed-tools` / `--allowedTools` | Not implemented |
| Decision ledger | Per-turn tool-approval record | Not implemented |
| MCP client | Model Context Protocol server integration | Not implemented |
| Plugin runtime hooks | Pre/post-tool callbacks in plugin runtime | Not implemented (metadata + registry scaffolding only) |

## Conventions for agents

- Keep changes minimal and reuse existing primitives (the runtime turn, the provider router,
  the control-sequence engine). Do not build a parallel agent engine.
- Do not commit build artifacts (`build/`), the compiled `ember` binary, or local tooling
  state (`.omc/`). See `.gitignore`.
- Verify before claiming: run the build and `ctest` and paste real output.
- Do not overstate provider support. Hosted providers require their API keys; the local
  default is Ollama.
