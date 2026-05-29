// test_real_tool_executor.cpp
//
// Unit tests for RealToolExecutor.
// Tests: read_file_round_trip, write_file_creates_file, bash_echo_returns_output.
// No external test framework — plain assert() and return 0/1.

#include "emberforge/tools/real_executor.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static std::string tmp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

int main() {
    emberforge::tools::RealToolExecutor executor;

    // ------------------------------------------------------------------
    // Test 1: read_file_round_trip
    // Write a file manually, then use read_file to verify content.
    // ------------------------------------------------------------------
    {
        // read_file is confined to the workspace (cwd), so use a relative
        // in-workspace path — same convention as the write_file test below.
        const std::string path = "emberforge_test_read.txt";
        const std::string expected = "Hello from read_file_round_trip!\n";

        {
            std::ofstream ofs(path);
            ofs << expected;
        }

        const std::string result = executor.execute("read_file", path);
        if (result != expected) {
            std::cerr << "FAIL (read_file_round_trip): expected \"" << expected
                      << "\" but got \"" << result << "\"\n";
            return 1;
        }
        std::filesystem::remove(path);
        std::cout << "PASS (read_file_round_trip)\n";
    }

    // ------------------------------------------------------------------
    // Test 2: write_file_creates_file
    // Use write_file to create a file, then verify content with ifstream.
    // ------------------------------------------------------------------
    {
        // write_file must stay within cwd — use a relative path so the
        // workspace check passes.
        const std::string rel_path = "emberforge_test_write.txt";
        const std::string content = "written by RealToolExecutor\n";
        const std::string input = rel_path + "\n" + content;

        const std::string result = executor.execute("write_file", input);
        if (result.find("[write_file] error") != std::string::npos) {
            std::cerr << "FAIL (write_file_creates_file): executor returned error: "
                      << result << "\n";
            return 1;
        }

        // Verify the file exists and has the right content.
        std::ifstream ifs(rel_path);
        if (!ifs.is_open()) {
            std::cerr << "FAIL (write_file_creates_file): file not created: " << rel_path << "\n";
            return 1;
        }
        const std::string file_content((std::istreambuf_iterator<char>(ifs)),
                                        std::istreambuf_iterator<char>());
        std::filesystem::remove(rel_path);

        if (file_content != content) {
            std::cerr << "FAIL (write_file_creates_file): expected \"" << content
                      << "\" but got \"" << file_content << "\"\n";
            return 1;
        }
        std::cout << "PASS (write_file_creates_file)\n";
    }

    // ------------------------------------------------------------------
    // Test 3: bash_echo_returns_output
    // Run `echo hi` and assert the output contains "hi".
    // ------------------------------------------------------------------
    {
        const std::string result = executor.execute("bash", "echo hi");
        if (result.find("hi") == std::string::npos) {
            std::cerr << "FAIL (bash_echo_returns_output): expected \"hi\" in output, got \""
                      << result << "\"\n";
            return 1;
        }
        std::cout << "PASS (bash_echo_returns_output)\n";
    }

    // ------------------------------------------------------------------
    // Test 4: read_file_rejects_absolute_outside_path
    // An absolute path outside the workspace must be refused (path-traversal
    // / arbitrary-read protection), mirroring write_file's boundary check.
    // ------------------------------------------------------------------
    {
        const std::string outside = tmp_path("emberforge_test_outside_secret.txt");
        const std::string secret = "outside-secret";
        {
            std::ofstream ofs(outside);
            ofs << secret;
        }

        const std::string result = executor.execute("read_file", outside);
        std::filesystem::remove(outside);

        if (result.find("[read_file] error: path is outside workspace") == std::string::npos) {
            std::cerr << "FAIL (read_file_rejects_absolute_outside_path): expected "
                         "outside-workspace rejection, got \"" << result << "\"\n";
            return 1;
        }
        if (result.find(secret) != std::string::npos) {
            std::cerr << "FAIL (read_file_rejects_absolute_outside_path): leaked file "
                         "content\n";
            return 1;
        }
        std::cout << "PASS (read_file_rejects_absolute_outside_path)\n";
    }

    // ------------------------------------------------------------------
    // Test 5: read_file_rejects_traversal_path
    // A traversal-shaped relative path that escapes the workspace must be
    // refused before the file is opened.
    // ------------------------------------------------------------------
    {
        const std::string traversal = "../emberforge_test_traversal_outside.txt";
        const std::string result = executor.execute("read_file", traversal);

        if (result.find("[read_file] error: path is outside workspace") == std::string::npos) {
            std::cerr << "FAIL (read_file_rejects_traversal_path): expected "
                         "outside-workspace rejection, got \"" << result << "\"\n";
            return 1;
        }
        std::cout << "PASS (read_file_rejects_traversal_path)\n";
    }

    // ------------------------------------------------------------------
    // Test 6: read_file_allows_in_workspace_path
    // An in-workspace read must still succeed after the boundary check.
    // ------------------------------------------------------------------
    {
        const std::string path = "emberforge_test_inworkspace.txt";
        const std::string expected = "inside-the-workspace\n";
        {
            std::ofstream ofs(path);
            ofs << expected;
        }

        const std::string result = executor.execute("read_file", path);
        std::filesystem::remove(path);

        if (result != expected) {
            std::cerr << "FAIL (read_file_allows_in_workspace_path): expected \"" << expected
                      << "\" but got \"" << result << "\"\n";
            return 1;
        }
        std::cout << "PASS (read_file_allows_in_workspace_path)\n";
    }

    std::cout << "All RealToolExecutor tests PASSED\n";
    return 0;
}
