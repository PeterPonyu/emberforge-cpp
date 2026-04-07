cmake_minimum_required(VERSION 3.20)
project(emberforge_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libcurl is required for OllamaProvider's HTTP streaming.
# Install on Debian/Ubuntu: sudo apt-get install libcurl4-openssl-dev
find_package(CURL REQUIRED)

add_library(emberforge_core
    src/api/provider.cpp
    src/api/ollama_provider.cpp
    src/commands/registry.cpp
    src/tools/registry.cpp
    src/runtime/runtime.cpp
    src/plugins/plugin.cpp
    src/plugins/registry.cpp
    src/telemetry/telemetry.cpp
    src/lsp/manager.cpp
    src/server/server.cpp
    src/system/control_sequence.cpp
    src/system/dispatch.cpp
    src/system/turn.cpp
    src/system/application.cpp
    src/compat/upstream_paths.cpp
)

target_include_directories(emberforge_core PUBLIC include)
target_link_libraries(emberforge_core PUBLIC CURL::libcurl)

add_executable(ember apps/ember_cli/main.cpp)
target_link_libraries(ember PRIVATE emberforge_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_ollama_provider tests/test_ollama_provider.cpp)
target_include_directories(test_ollama_provider PRIVATE include)
target_link_libraries(test_ollama_provider PRIVATE emberforge_core)

add_test(NAME test_ollama_provider COMMAND test_ollama_provider)
