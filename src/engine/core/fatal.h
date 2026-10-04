#pragma once

namespace engine {

// Prints the message to stderr and exits with code 1, breaking into a debugger first when one is attached
[[noreturn]] void fatal(const char* format, ...);

}
