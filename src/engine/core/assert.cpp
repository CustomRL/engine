#include "engine/core/assert.h"

#include "engine/core/fatal.h"

#include <cstdarg>
#include <cstdio>

namespace engine {

void assertFailed(const char* file, int line, const char* condition, const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof message, format, args);
    va_end(args);
    fatal("assert failed at %s:%d: (%s) %s", file, line, condition, message);
}

}
