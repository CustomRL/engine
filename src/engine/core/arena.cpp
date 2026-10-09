#include "engine/core/arena.h"

#include "engine/core/fatal.h"

#include <windows.h>

namespace engine {

constexpr uintptr_t kCommitStep = 64 * 1024;  // commit in 64 KiB steps so a growing arena calls VirtualAlloc rarely

Arena::Arena(size_t reserveBytes) {
    const size_t bytes = (reserveBytes + kCommitStep - 1) & ~(kCommitStep - 1);
    void* p = VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_NOACCESS);
    if (!p)
        fatal("Arena: can't reserve %zu bytes", bytes);
    base_ = top_ = committed_ = reinterpret_cast<uintptr_t>(p);
    end_ = base_ + bytes;
}

Arena::~Arena() {
    VirtualFree(reinterpret_cast<void*>(base_), 0, MEM_RELEASE);
}

void Arena::commit(uintptr_t end) {
    if (end > end_)
        fatal("Arena: %zu bytes asked of a %zu-byte reservation", size_t(end - base_), size_t(end_ - base_));
    const uintptr_t to = end_ - end < kCommitStep ? end_ : (end + kCommitStep - 1) & ~(kCommitStep - 1);
    if (!VirtualAlloc(reinterpret_cast<void*>(committed_), to - committed_, MEM_COMMIT, PAGE_READWRITE))
        fatal("Arena: can't commit %zu bytes", size_t(to - base_));
    committed_ = to;
}

}
