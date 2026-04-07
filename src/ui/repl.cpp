#include "emberforge/ui/repl.hpp"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "emberforge/api/provider.hpp"
#include "emberforge/system/application.hpp"

namespace emberforge::ui {

// ---------------------------------------------------------------------------
// RAII guard: enables termios raw mode in ctor, restores original in dtor.
// ---------------------------------------------------------------------------
class RawMode {
public:
    RawMode() {
        tcgetattr(STDIN_FILENO, &original_);
        struct termios raw = original_;
        // Disable canonical mode, echo; enable raw character-at-a-time input.
        raw.c_iflag &= ~static_cast<tcflag_t>(ICRNL | IXON);
        raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
        raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    ~RawMode() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    }

    // Non-copyable, non-movable.
    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;

private:
    struct termios original_;
};

Repl::Repl(emberforge::system::StarterSystemApplication& app)
    : app_(app), dispatch_() {}

static void print_prompt() {
    std::cout << "ember> " << std::flush;
}

// Reprint the current line after a buffer modification.
static void redraw_line(const std::string& buf, std::size_t cursor) {
    std::cout << "\r\x1b[K" << "ember> " << buf << std::flush;
    if (cursor < buf.size()) {
        const std::size_t steps = buf.size() - cursor;
        std::cout << "\x1b[" << steps << "D" << std::flush;
    }
}

static std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) {
        parts.push_back(tok);
    }
    return parts;
}

int Repl::run() {
    // Do not enable raw mode if stdin is not a TTY (e.g. piped input in tests).
    const bool is_tty = isatty(STDIN_FILENO);

    std::unique_ptr<RawMode> raw;
    if (is_tty) {
        raw = std::make_unique<RawMode>();
    }

    std::vector<std::string> history;
    history.reserve(100);
    int history_idx = -1; // -1 means "at the live line"

    std::string buf;       // current line buffer
    std::string saved_buf; // saved line when navigating history
    std::size_t cursor = 0;

    print_prompt();

    while (true) {
        char c = '\0';
        const ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            // EOF or error
            std::cout << '\n';
            return 0;
        }

        // ---- Ctrl-D (0x04) — EOF ----------------------------------------
        if (c == 0x04) {
            std::cout << '\n';
            return 0;
        }

        // ---- Ctrl-C (0x03) — clear line, do NOT exit --------------------
        if (c == 0x03) {
            std::cout << "^C\n";
            buf.clear();
            cursor = 0;
            history_idx = -1;
            print_prompt();
            continue;
        }

        // ---- Enter (\r or \n) — commit line ------------------------------
        if (c == '\r' || c == '\n') {
            std::cout << '\n';

            if (!buf.empty()) {
                // Add to history (ring buffer, max 100).
                if (history.size() >= 100) {
                    history.erase(history.begin());
                }
                history.push_back(buf);
                history_idx = -1;

                // Dispatch
                if (buf[0] == '/') {
                    const std::string body = buf.substr(1); // strip leading '/'
                    auto parts = split_args(body);
                    if (parts.empty()) {
                        std::cerr << "empty command\n";
                    } else {
                        const std::string cmd = parts[0];
                        parts.erase(parts.begin());

                        if (cmd == "help") {
                            std::cout << "available commands:\n";
                            for (const auto& [name, _] : dispatch_.handlers()) {
                                std::cout << "  /" << name << '\n';
                            }
                        } else {
                            const int rc = dispatch_.invoke(cmd, app_, parts);
                            if (rc == 255) {
                                // /quit
                                return 0;
                            }
                        }
                    }
                } else {
                    // Non-slash line: send to provider.
                    const char* env_model = std::getenv("EMBER_MODEL");
                    const std::string model = env_model ? env_model : "qwen3:8b";
                    try {
                        emberforge::api::MessageRequest req{model, buf};
                        auto response = app_.provider().send_message(req);
                        std::cout << response.text << '\n';
                    } catch (const std::exception& ex) {
                        std::cerr << "[repl] error: " << ex.what() << '\n';
                    }
                }
            }

            buf.clear();
            cursor = 0;
            print_prompt();
            continue;
        }

        // ---- Backspace (0x7f or 0x08) ------------------------------------
        if (c == 0x7f || c == 0x08) {
            if (cursor > 0) {
                buf.erase(cursor - 1, 1);
                --cursor;
                if (is_tty) {
                    redraw_line(buf, cursor);
                }
            }
            continue;
        }

        // ---- ESC sequence (arrow keys) ------------------------------------
        if (c == 0x1b) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A': { // Up arrow — history back
                        if (history.empty()) break;
                        if (history_idx == -1) {
                            saved_buf = buf;
                            history_idx = static_cast<int>(history.size()) - 1;
                        } else if (history_idx > 0) {
                            --history_idx;
                        }
                        buf = history[static_cast<std::size_t>(history_idx)];
                        cursor = buf.size();
                        if (is_tty) redraw_line(buf, cursor);
                        break;
                    }
                    case 'B': { // Down arrow — history forward
                        if (history_idx == -1) break;
                        if (history_idx < static_cast<int>(history.size()) - 1) {
                            ++history_idx;
                            buf = history[static_cast<std::size_t>(history_idx)];
                        } else {
                            history_idx = -1;
                            buf = saved_buf;
                        }
                        cursor = buf.size();
                        if (is_tty) redraw_line(buf, cursor);
                        break;
                    }
                    case 'C': { // Right arrow
                        if (cursor < buf.size()) {
                            ++cursor;
                            if (is_tty) std::cout << "\x1b[C" << std::flush;
                        }
                        break;
                    }
                    case 'D': { // Left arrow
                        if (cursor > 0) {
                            --cursor;
                            if (is_tty) std::cout << "\x1b[D" << std::flush;
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
            continue;
        }

        // ---- Printable character: insert at cursor -----------------------
        if (static_cast<unsigned char>(c) >= 0x20) {
            buf.insert(cursor, 1, c);
            ++cursor;
            if (is_tty) {
                redraw_line(buf, cursor);
            } else {
                std::cout << c << std::flush;
            }
        }
    }
}

} // namespace emberforge::ui
