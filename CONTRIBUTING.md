# Contributing

Thanks for contributing to Emberforge (C++).

## Development setup

- C++20-capable compiler (GCC 10+ or Clang 12+)
- CMake 3.20+
- libcurl (`libcurl4-openssl-dev` on Debian/Ubuntu)
- nlohmann/json (`nlohmann-json3-dev` on Debian/Ubuntu, v3.11.3+)

Install on Debian/Ubuntu:

```bash
sudo apt-get install -y cmake libcurl4-openssl-dev nlohmann-json3-dev
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The `ember` binary is produced at `build/ember` — there is no install step.

## Test

Run the full test suite before opening a pull request:

```bash
ctest --test-dir build --output-on-failure
```

All tests must pass. If you add new behaviour, add a corresponding test in `tests/`
and register it in `CMakeLists.txt` mirroring the existing `add_executable` / `add_test`
pairs (link against `emberforge_core`, include `include/`).

## Code style

- Match the style of the file you are editing (naming, error handling, include order).
- Prefer focused diffs over drive-by refactors.
- Do not commit build artefacts (`build/`), the compiled binary, or local tooling state (`.omc/`).

## Pull requests

- Branch from `main` (or `feat/local-model-parity` if porting a parity feature).
- Keep each pull request scoped to one clear change.
- Describe the motivation, the implementation summary, and the `ctest` output you observed.
- Make sure the build and tests pass locally before requesting review.

## Parity work

When porting a feature from the Rust reference, update the parity table in both
`README.md` and `AGENTS.md` to reflect the new status.
