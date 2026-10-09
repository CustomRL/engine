#include "engine/core/log.h"

#include <windows.h>

#include <share.h>

#include <cstdio>
#include <mutex>

namespace engine {

namespace {

const char* const kLevelNames[] = {"ERROR", "WARN", "INFO", "VERBOSE", "DEBUG"};

std::mutex g_mutex;
FILE* g_file = nullptr;
char g_fileBuffer[4096];  // static so stdio doesn't allocate one on first write
LogSink g_sink = defaultLogSink;

}

void setLogSink(LogSink sink) {
    g_sink = sink ? sink : defaultLogSink;
}

bool openLogFile(const wchar_t* path) {
    std::lock_guard lock(g_mutex);
    if (g_file)
        std::fclose(g_file);
    // binary: lines end in \n only, so the file reads the same everywhere
    // readable while open, so tools can tail it
    g_file = _wfsopen(path, L"wb", _SH_DENYNO);
    if (!g_file)
        return false;
    std::setvbuf(g_file, g_fileBuffer, _IOFBF, sizeof g_fileBuffer);
    return true;
}

void defaultLogSink(LogLevel level, const char* line) {
    static const ULONGLONG start = GetTickCount64();
    const double t = double(GetTickCount64() - start) / 1000.0;
    std::lock_guard lock(g_mutex);
    FILE* console = level <= LogLevel::Warn ? stderr : stdout;
    std::fprintf(console, "[%9.3f] %s\n", t, line);
    std::fflush(console);
    if (g_file) {
        std::fprintf(g_file, "[%9.3f] %s\n", t, line);
        std::fflush(g_file);  // a crash right after a line must not lose it
    }
}

void logWriteV(LogLevel level, const char* category, const char* format, va_list args) {
    char line[2048];
    int n = std::snprintf(line, sizeof line, "%-7s %s: ", kLevelNames[int(level)], category);
    if (n < 0 || n >= int(sizeof line))
        n = 0;
    std::vsnprintf(line + n, sizeof line - n, format, args);
    g_sink(level, line);
}

void logWrite(LogLevel level, const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logWriteV(level, category, format, args);
    va_end(args);
}

}
