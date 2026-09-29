#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "command.hpp"
#include "snailtrail/util/strings.hpp"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

bool stdout_is_terminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

void prepare_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

std::size_t terminal_width() {
    if (const char* columns = std::getenv("COLUMNS")) {
        if (const auto n = snailtrail::util::parse_uint(columns); n && *n >= 60 && *n <= 400) {
            return static_cast<std::size_t>(*n);
        }
    }
    return 100;
}

}

int main(int argc, char** argv) {
    prepare_console();
    std::ios::sync_with_stdio(false);
    const std::vector<std::string> args(argv + 1, argv + argc);
    const bool terminal = stdout_is_terminal();
    snailtrail::cli::Console console{std::cout, std::cerr, std::cin,
                                     terminal && std::getenv("NO_COLOR") == nullptr, terminal_width()};
    const int code = snailtrail::cli::App().run(args, console);
    std::cout.flush();
    return code;
}
