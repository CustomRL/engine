#include "engine/core/fatal.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace engine {

void fatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    if (IsDebuggerPresent())
        __debugbreak();
    // not abort(): a release build hands an abort to Windows Error Reporting, which holds the process and can raise a dialog
    std::_Exit(1);
}

}
