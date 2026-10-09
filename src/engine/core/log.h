#pragma once

#include <sal.h>

#include <cstdarg>

namespace engine {

enum class LogLevel { Error, Warn, Info, Verbose, Debug };

// Receives one finished line ("LEVEL category: message", no newline). A DLL has its own copy of the engine, so the host hands it its
// sink (default: console and the log file) and its logs land in the same file.
using LogSink = void (*)(LogLevel level, const char* line);

void setLogSink(LogSink sink);
void defaultLogSink(LogLevel level, const char* line);  // timestamps the line; errors and warnings to stderr, the rest to stdout
bool openLogFile(const wchar_t* path);                  // the default sink also writes to this file (truncated); false if it can't open

// A message longer than the 2 KiB stack buffer is cut. Never touches the heap once openLogFile has run.
void logWrite(LogLevel level, const char* category, _Printf_format_string_ const char* format, ...);
void logWriteV(LogLevel level, const char* category, const char* format, va_list args);

}

#define ENGINE_LOG_ERROR(category, ...) engine::logWrite(engine::LogLevel::Error, category, __VA_ARGS__)
#define ENGINE_LOG_WARN(category, ...) engine::logWrite(engine::LogLevel::Warn, category, __VA_ARGS__)
#define ENGINE_LOG_INFO(category, ...) engine::logWrite(engine::LogLevel::Info, category, __VA_ARGS__)

// Verbose and debug lines compile out (strings included) when ENGINE_DIAGNOSTICS is off, as in the perf preset
#ifdef ENGINE_DIAGNOSTICS
#define ENGINE_LOG_VERBOSE(category, ...) engine::logWrite(engine::LogLevel::Verbose, category, __VA_ARGS__)
#define ENGINE_LOG_DEBUG(category, ...) engine::logWrite(engine::LogLevel::Debug, category, __VA_ARGS__)
#else
#define ENGINE_LOG_VERBOSE(category, ...) ((void)0)
#define ENGINE_LOG_DEBUG(category, ...) ((void)0)
#endif
