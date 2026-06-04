cmake_minimum_required(VERSION 3.20)
project(emberforge_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libcurl is required for OllamaProvider's HTTP streaming.
# Install on Debian/Ubuntu: sudo apt-get install libcurl4-openssl-dev
find_package(CURL REQUIRED)

# nlohmann/json is used for NDJSON parsing in OllamaProvider.
# Install on Debian/Ubuntu: sudo apt-get install nlohmann-json3-dev
find_package(nlohmann_json REQUIRED)

add_library(emberforge_core
    src/api/provider.cpp
    src/api/ollama_provider.cpp
    src/api/hosted_provider.cpp
    src/api/provider_router.cpp
    src/commands/registry.cpp
    src/tools/registry.cpp
    src/tools/real_executor.cpp
    src/runtime/runtime.cpp
    src/runtime/system_prompt.cpp
    src/plugins/plugin.cpp
    src/plugins/registry.cpp
    src/plugins/hook_event.cpp
    src/plugins/hook.cpp
    src/plugins/hook_executor.cpp
    src/plugins/hook_runner.cpp
    src/telemetry/telemetry.cpp
    src/lsp/manager.cpp
    src/server/server.cpp
    src/system/control_sequence.cpp
    src/system/buddy.cpp
    src/system/task_question_state.cpp
    src/system/doctor.cpp
    src/system/dispatch.cpp
    src/system/turn.cpp
    src/system/application.cpp
    src/compat/upstream_paths.cpp
    src/ui/repl.cpp
    src/ui/command_dispatch.cpp
    src/persistence/session_store.cpp
)

target_include_directories(emberforge_core PUBLIC include)
target_link_libraries(emberforge_core PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)

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

add_executable(test_real_tool_executor tests/test_real_tool_executor.cpp)
target_include_directories(test_real_tool_executor PRIVATE include)
target_link_libraries(test_real_tool_executor PRIVATE emberforge_core)

add_test(NAME test_real_tool_executor COMMAND test_real_tool_executor)

add_executable(test_command_dispatch tests/test_command_dispatch.cpp)
target_include_directories(test_command_dispatch PRIVATE include)
target_link_libraries(test_command_dispatch PRIVATE emberforge_core)

add_test(NAME test_command_dispatch COMMAND test_command_dispatch)

add_executable(test_session_store tests/test_session_store.cpp)
target_include_directories(test_session_store PRIVATE include)
target_link_libraries(test_session_store PRIVATE emberforge_core)

add_test(NAME test_session_store COMMAND test_session_store)

add_executable(test_doctor tests/test_doctor.cpp)
target_include_directories(test_doctor PRIVATE include)
target_link_libraries(test_doctor PRIVATE emberforge_core)

add_test(NAME test_doctor COMMAND test_doctor)

add_executable(test_hook_engine tests/test_hook_engine.cpp)
target_include_directories(test_hook_engine PRIVATE include)
target_link_libraries(test_hook_engine PRIVATE emberforge_core)

add_test(NAME test_hook_engine COMMAND test_hook_engine)

add_executable(test_upstream_paths tests/test_upstream_paths.cpp)
target_include_directories(test_upstream_paths PRIVATE include)
target_link_libraries(test_upstream_paths PRIVATE emberforge_core)

add_test(NAME test_upstream_paths COMMAND test_upstream_paths)

add_executable(test_telemetry tests/test_telemetry.cpp)
target_include_directories(test_telemetry PRIVATE include)
target_link_libraries(test_telemetry PRIVATE emberforge_core)

add_test(NAME test_telemetry COMMAND test_telemetry)

add_executable(test_lsp_manager tests/test_lsp_manager.cpp)
target_include_directories(test_lsp_manager PRIVATE include)
target_link_libraries(test_lsp_manager PRIVATE emberforge_core)

add_test(NAME test_lsp_manager COMMAND test_lsp_manager)

add_executable(test_provider_router tests/test_provider_router.cpp)
target_include_directories(test_provider_router PRIVATE include)
target_link_libraries(test_provider_router PRIVATE emberforge_core)

add_test(NAME test_provider_router COMMAND test_provider_router)

add_executable(test_tool_registry tests/test_tool_registry.cpp)
target_include_directories(test_tool_registry PRIVATE include)
target_link_libraries(test_tool_registry PRIVATE emberforge_core)

add_test(NAME test_tool_registry COMMAND test_tool_registry)

add_executable(test_system_prompt tests/test_system_prompt.cpp)
target_include_directories(test_system_prompt PRIVATE include)
target_link_libraries(test_system_prompt PRIVATE emberforge_core)

add_test(NAME test_system_prompt COMMAND test_system_prompt)

add_executable(test_agentic_loop tests/test_agentic_loop.cpp)
target_include_directories(test_agentic_loop PRIVATE include)
target_link_libraries(test_agentic_loop PRIVATE emberforge_core)

add_test(NAME test_agentic_loop COMMAND test_agentic_loop)

add_executable(test_prompt_command tests/test_prompt_command.cpp)
target_include_directories(test_prompt_command PRIVATE include)
target_link_libraries(test_prompt_command PRIVATE emberforge_core)
# Pass the built `ember` binary path so the test can exercise the real
# `prompt` subcommand argument handling (no-text error path, no Ollama needed).
target_compile_definitions(test_prompt_command PRIVATE EMBER_BINARY="$<TARGET_FILE:ember>")

add_test(NAME test_prompt_command COMMAND test_prompt_command)
