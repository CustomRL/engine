#pragma once

#include <sal.h>

namespace engine {

// Logs the message as an error (console and log file) and exits with code 1, breaking into a debugger first when one is attached
[[noreturn]] void fatal(_Printf_format_string_ const char* format, ...);

}
