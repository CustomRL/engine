#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

namespace engine {

// A linear allocator over one reserved range of address space: an allocation is a pointer bump, pages are committed as the top grows,
// and reset frees everything above a mark at once. One per frame (reset every frame) and one per level (reset on unload). Running past
// the reservation is fatal. Not thread-safe: one arena per thread.
class Arena {
public:
    explicit Arena(size_t reserveBytes);
    ~Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* alloc(size_t bytes, size_t align = 16) {
        const uintptr_t at = (top_ + (align - 1)) & ~uintptr_t(align - 1);
        if (at + bytes > committed_)
            commit(at + bytes);
        top_ = at + bytes;
        return reinterpret_cast<void*>(at);
    }
    // n default-initialized Ts (trivial types stay uninitialized); the arena never runs destructors
    template <class T>
    T* make(size_t n = 1) {
        T* p = static_cast<T*>(alloc(sizeof(T) * n, alignof(T)));
        for (size_t i = 0; i < n; i++)  // not placement new[], which may put a size cookie in front
            new (p + i) T;
        return p;
    }

    size_t mark() const { return top_ - base_; }
    void reset(size_t mark = 0) { top_ = base_ + mark; }  // pages stay committed for the next use
    size_t used() const { return top_ - base_; }
    size_t committed() const { return committed_ - base_; }

private:
    void commit(uintptr_t end);

    uintptr_t base_ = 0, top_ = 0, committed_ = 0, end_ = 0;
};

// A fixed pool of N slots of T with an intrusive free list: alloc and free are O(1) and touch one slot. alloc returns raw storage
// (construct it in place) or null when all N are taken.
template <class T, uint32_t N>
class Pool {
public:
    Pool() {
        for (uint32_t i = 0; i < N; i++)
            *reinterpret_cast<uint32_t*>(slots_[i].bytes) = i + 1;
    }
    T* alloc() {
        if (free_ == N)
            return nullptr;
        Slot& s = slots_[free_];
        free_ = *reinterpret_cast<uint32_t*>(s.bytes);
        used_++;
        return reinterpret_cast<T*>(s.bytes);
    }
    void free(T* p) {
        const uint32_t i = uint32_t(reinterpret_cast<Slot*>(p) - slots_);
        *reinterpret_cast<uint32_t*>(slots_[i].bytes) = free_;
        free_ = i;
        used_--;
    }
    uint32_t used() const { return used_; }
    static constexpr uint32_t capacity() { return N; }
    uint32_t index(const T* p) const { return uint32_t(reinterpret_cast<const Slot*>(p) - slots_); }

private:
    struct alignas(alignof(T) > 4 ? alignof(T) : 4) Slot {
        unsigned char bytes[sizeof(T) > 4 ? sizeof(T) : 4];
    };
    Slot slots_[N];
    uint32_t free_ = 0, used_ = 0;
};

// operator new calls made by the calling module (exe or DLL) since it started: every module that links engine_alloc_count counts its
// own, and returns 0 when built without ENGINE_COUNT_ALLOCS
uint64_t allocationCount();

}
