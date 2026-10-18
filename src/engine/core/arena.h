#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

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

// A vector of trivially copyable Ts kept in an Arena: growing takes a block twice the size and leaves the old one in the arena, so it
// never calls operator new. For lists cleared and refilled up to a high water (a frame's draw commands, a text's layout): the arena
// holds what each grew through, under twice its high water. A copy takes its own block from the same arena.
template <class T>
class GrowArray {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    explicit GrowArray(Arena& arena) : arena_(&arena) {}
    GrowArray(const GrowArray& o) : arena_(o.arena_) { assign(o.p_, o.n_); }
    GrowArray(GrowArray&& o) noexcept : arena_(o.arena_), p_(o.p_), n_(o.n_), cap_(o.cap_) { o.p_ = nullptr, o.n_ = o.cap_ = 0; }
    GrowArray& operator=(const GrowArray& o) { return this != &o ? assign(o.p_, o.n_) : *this; }
    GrowArray& operator=(GrowArray&& o) noexcept {
        std::swap(arena_, o.arena_), std::swap(p_, o.p_), std::swap(n_, o.n_), std::swap(cap_, o.cap_);
        return *this;
    }

    T* begin() { return p_; }
    T* end() { return p_ + n_; }
    const T* begin() const { return p_; }
    const T* end() const { return p_ + n_; }
    T* data() { return p_; }
    const T* data() const { return p_; }
    size_t size() const { return n_; }
    bool empty() const { return !n_; }
    T& operator[](size_t k) { return p_[k]; }
    const T& operator[](size_t k) const { return p_[k]; }
    T& back() { return p_[n_ - 1]; }

    void clear() { n_ = 0; }
    void reserve(size_t n) {
        if (n > cap_)
            grow(n);
    }
    void resize(size_t n) {  // new ones value-initialized
        reserve(n);
        for (size_t k = n_; k < n; k++)
            p_[k] = T{};
        n_ = n;
    }
    void push_back(const T& v) {  // v may be one of its own: a block grow leaves stays readable
        if (n_ == cap_)
            grow(n_ + 1);
        p_[n_++] = v;
    }
    void pop_back() { n_--; }
    GrowArray& assign(const T* v, size_t n) {
        n_ = 0;
        reserve(n);
        if (n)
            std::memmove(p_, v, sizeof(T) * n);
        n_ = n;
        return *this;
    }
    T* insert(T* at, const T& v) {
        const size_t k = size_t(at - p_);
        const T copy = v;  // v may be one of those the move shifts
        reserve(n_ + 1);
        std::memmove(p_ + k + 1, p_ + k, sizeof(T) * (n_ - k));
        p_[k] = copy, n_++;
        return p_ + k;
    }
    T* erase(T* first, T* last) {
        std::memmove(first, last, sizeof(T) * size_t(end() - last));
        n_ -= size_t(last - first);
        return first;
    }
    T* erase(T* at) { return erase(at, at + 1); }

private:
    void grow(size_t need) {
        const size_t cap = need > cap_ * 2 ? (need > 8 ? need : 8) : cap_ * 2;
        T* p = static_cast<T*>(arena_->alloc(sizeof(T) * cap, alignof(T)));
        if (n_)
            std::memcpy(p, p_, sizeof(T) * n_);
        p_ = p, cap_ = cap;
    }

    Arena* arena_;
    T* p_ = nullptr;
    size_t n_ = 0, cap_ = 0;
};

// operator new calls made by the calling module (exe or DLL) since it started: every module that links engine_alloc_count counts its
// own, and returns 0 when built without ENGINE_COUNT_ALLOCS
uint64_t allocationCount();

}
