#include "engine/core/fatal.h"

#include "engine/core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdlib>

namespace engine {

void fatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logWriteV(LogLevel::Error, "fatal", format, args);
    va_end(args);
    if (IsDebuggerPresent())
        __debugbreak();
    // not abort(): a release build hands an abort to Windows Error Reporting, which holds the process and can raise a dialog
    std::_Exit(1);
}

}
