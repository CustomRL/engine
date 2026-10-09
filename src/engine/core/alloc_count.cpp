// Linked as an object library into each exe and DLL that counts its allocations, because a DLL has its own operator new.
#include "engine/core/arena.h"

#ifdef ENGINE_COUNT_ALLOCS

#include <atomic>
#include <cstdlib>
#include <malloc.h>

static std::atomic<uint64_t> g_allocations{0};

// The CRT's array, nothrow and sized forms all route through these two, and its deletes free what malloc and _aligned_malloc return,
// so only the two allocating forms are replaced.
void* operator new(size_t bytes) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(bytes ? bytes : 1))
        return p;
    throw std::bad_alloc();
}

void* operator new(size_t bytes, std::align_val_t align) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = _aligned_malloc(bytes ? bytes : 1, static_cast<size_t>(align)))
        return p;
    throw std::bad_alloc();
}

uint64_t engine::allocationCount() {
    return g_allocations.load(std::memory_order_relaxed);
}

#else

uint64_t engine::allocationCount() {
    return 0;
}

#endif
