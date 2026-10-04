#include "engine/core/fatal.h"

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
    std::abort();
}

}
