// engine_check's logging and assert cases. In a build without ENGINE_DIAGNOSTICS (perf) they instead prove that ENGINE_VERIFY still runs
// its expression and that the assert and verbose-log strings are not in the exe.
#include "engine/core/arena.h"
#include "engine/core/assert.h"
#include "engine/core/fatal.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#define CHECK(c)                                                                                                                   \
    do {                                                                                                                           \
        if (!(c))                                                                                                                  \
            engine::fatal("engine_check: %s failed (line %d)", #c, __LINE__);                                                     \
    } while (0)

using namespace engine;

static std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The marker text is built from two literals so only the assert and log calls below hold it whole
static std::string marker(const char* kind) {
    return std::string("ENGINE_CHECK_") + kind + "_7f3a";
}

// Run as a child process by assertFailure: logs to the given file, then fails an assert
[[noreturn]] void assertChild(const char* logPath) {
    openLogFile(std::filesystem::path(logPath).c_str());
    ENGINE_LOG_VERBOSE("chk", "ENGINE_CHECK_%s_7f3a", "VERBOSE");
    std::printf("expected line %d\n", __LINE__ + 1);
    ENGINE_ASSERT(1 + 1 == 3, "ENGINE_CHECK_%s_7f3a %d", "ASSERT", 42);
    std::exit(77);  // reached only when asserts are compiled out
}

static void logging(const std::filesystem::path& dir) {
    const auto path = dir / "engine_check.log";
    CHECK(openLogFile(path.c_str()));
    ENGINE_LOG_ERROR("alpha", "error %d", 1);
    ENGINE_LOG_WARN("beta", "warn %s", "two");
    ENGINE_LOG_INFO("gamma", "info %.1f", 3.0);
    ENGINE_LOG_VERBOSE("delta", "verbose %d", 4);
    ENGINE_LOG_DEBUG("epsilon", "debug %d", 5);
    const std::string text = slurp(path);
    CHECK(text.find("ERROR   alpha: error 1\n") != std::string::npos);
    CHECK(text.find("WARN    beta: warn two\n") != std::string::npos);
    CHECK(text.find("INFO    gamma: info 3.0\n") != std::string::npos);
#ifdef ENGINE_DIAGNOSTICS
    CHECK(text.find("VERBOSE delta: verbose 4\n") != std::string::npos && text.find("DEBUG   epsilon: debug 5\n") != std::string::npos);
#else
    CHECK(text.find("delta") == std::string::npos && text.find("epsilon") == std::string::npos);
#endif

    // a message longer than the line buffer is cut to one line, and formatting it costs no operator new
    const std::string big(5000, 'x');
    const uint64_t before = allocationCount();
    ENGINE_LOG_INFO("long", "%s", big.c_str());
    ENGINE_LOG_INFO("long", "%s|%d|%s", big.c_str(), 7, big.c_str());
    CHECK(allocationCount() == before);
    const std::string added = slurp(path).substr(text.size());
    CHECK(std::count(added.begin(), added.end(), '\n') == 2 && added.size() < 4300 && added.find("INFO    long: xxx") != std::string::npos);
    std::printf("logging: ok (levels, categories, file; long messages cut and %s)\n",
                before ? "counted 0 operator new calls" : "allocation counter off");
}

#ifdef ENGINE_DIAGNOSTICS
static void assertFailure(const std::filesystem::path& dir, const char* self) {
    const auto log = dir / "engine_check_assert.log", out = dir / "engine_check_assert.out";
    const std::wstring cmd = L"\"\"" + std::filesystem::path(self).wstring() + L"\" --assert-fail \"" + log.wstring() + L"\" > \"" +
                             out.wstring() + L"\" 2>&1\"";
    CHECK(_wsystem(cmd.c_str()) == 1);  // fatal's exit code
    const std::string console = slurp(out);
    const size_t at = console.find("expected line ");
    CHECK(at != std::string::npos);
    const std::string where = "log_assert_check.cpp:" + std::to_string(std::atoi(console.c_str() + at + 14));
    for (const std::string& text : {console, slurp(log)}) {
        CHECK(text.find("ERROR   fatal: assert failed at ") != std::string::npos);
        CHECK(text.find(where + ": (1 + 1 == 3) " + marker("ASSERT") + " 42") != std::string::npos);
    }
    CHECK(slurp(log).find(marker("VERBOSE")) != std::string::npos);
    std::printf("assert: ok (child exited 1; file, line, condition and message on the console and in the log)\n");
}
#else
static void stripped(const char* self) {
    int runs = 0;
    ENGINE_VERIFY(++runs == 1, "never shown");
    ENGINE_ASSERT(++runs == 99, "never evaluated");
    CHECK(runs == 1);
    const std::string exe = slurp(self);
    CHECK(!exe.empty() && exe.find(marker("ASSERT")) == std::string::npos && exe.find(marker("VERBOSE")) == std::string::npos);
    std::printf("diagnostics off: ok (VERIFY ran, ASSERT did not; assert and verbose strings absent from the exe)\n");
}
#endif

void logAndAssertChecks(const char* self) {
    const auto dir = std::filesystem::temp_directory_path();
    logging(dir);
#ifdef ENGINE_DIAGNOSTICS
    assertFailure(dir, self);
    int runs = 0;
    ENGINE_VERIFY(++runs == 1, "verify");
    CHECK(runs == 1);
#else
    stripped(self);
#endif
}
