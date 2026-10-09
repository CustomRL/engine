#pragma once

#include "engine/core/log.h"

namespace engine {

// Logs the file, line, condition and message, then breaks into a debugger if one is attached, else exits (see fatal)
[[noreturn]] void assertFailed(const char* file, int line, const char* condition, _Printf_format_string_ const char* format, ...);

}

// ENGINE_ASSERT(cond, "format", args...): checked when ENGINE_DIAGNOSTICS is on; otherwise the condition and strings are gone.
// ENGINE_VERIFY keeps evaluating the condition when it is off, for expressions with side effects.
#ifdef ENGINE_DIAGNOSTICS
#define ENGINE_ASSERT(cond, ...)                                          \
    do {                                                                  \
        if (!(cond))                                                      \
            engine::assertFailed(__FILE__, __LINE__, #cond, __VA_ARGS__); \
    } while (0)
#define ENGINE_VERIFY(cond, ...) ENGINE_ASSERT(cond, __VA_ARGS__)
#else
#define ENGINE_ASSERT(cond, ...) ((void)0)
#define ENGINE_VERIFY(cond, ...) ((void)(cond))
#endif
