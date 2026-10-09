#pragma once

#include <cstddef>
#include <cstdint>

// Pack format, version 1. A pack is one file the loader maps into memory as is: nothing in it is parsed or copied at runtime.
//
//   offset 0    PackHeader (64 bytes)
//   64          ids[count]      u64, strictly ascending: the lookup key, searched on its own so a lookup touches 8 bytes a step
//   ...         entries[count]  PackEntry (16 bytes), entries[i] belongs to ids[i]
//   ...         zero padding to 4096
//   blobs       each blob starts on a 4096-byte boundary from the file's start and is followed by zero padding to the next one;
//               the file ends on a 4096-byte boundary
//
// All integers are little-endian. The mapped view starts page-aligned, so every blob starts page-aligned in memory and can back a
// GPU upload or a further mapping directly. Inside a blob, references are self-relative (RelPtr below), never absolute pointers, so a
// blob is valid wherever it is mapped. An asset id is opaque to the engine; the cooker picks it (a hash of the source object path).
// type is the cooker's asset type code; the engine passes it through.
namespace engine::assets {

constexpr uint32_t kPackMagic = 0x4b415045;  // "EPAK" in file order
constexpr uint32_t kPackVersion = 1;
constexpr uint64_t kPackAlign = 4096;

struct PackHeader {
    uint32_t magic;
    uint32_t version;
    char build[32];      // the build the pack was cooked from, NUL-padded text; the loader compares it, never parses it
    uint32_t count;      // assets
    uint32_t reserved;   // 0
    uint64_t fileSize;   // the whole file, a multiple of kPackAlign
    uint64_t blobStart;  // first blob's offset, a multiple of kPackAlign
};
static_assert(sizeof(PackHeader) == 64);

struct PackEntry {
    uint64_t offset;  // from the file's start, a multiple of kPackAlign
    uint32_t size;    // bytes, without the padding
    uint32_t type;
};
static_assert(sizeof(PackEntry) == 16);

// A reference inside a blob: the target's distance in bytes from this field, 0 for null
template <class T>
struct RelPtr {
    int64_t delta;
    const T* get() const { return delta ? reinterpret_cast<const T*>(reinterpret_cast<const char*>(this) + delta) : nullptr; }
};

struct Blob {
    const void* data = nullptr;  // null when the id is not in the pack
    uint32_t size = 0;
    uint32_t type = 0;
};

// One mapped pack. open checks the header, the table and every entry's bounds once, so find never checks them again.
class Pack {
public:
    Pack() = default;
    ~Pack() { close(); }
    Pack(const Pack&) = delete;
    Pack& operator=(const Pack&) = delete;

    // False with a message in error (when given) if the file can't be mapped or isn't a valid version-1 pack
    bool open(const wchar_t* path, const char** error = nullptr);
    void close();

    const PackHeader* header() const { return reinterpret_cast<const PackHeader*>(base_); }
    uint32_t count() const { return count_; }
    const uint64_t* ids() const { return ids_; }
    const PackEntry* entries() const { return entries_; }

    // Binary search over ids; no allocation, no copy
    Blob find(uint64_t id) const {
        const uint64_t* lo = ids_;
        size_t n = count_;
        while (n > 1) {  // branchless halving: the compiler turns the select into cmov
            const size_t half = n / 2;
            lo = lo[half] <= id ? lo + half : lo;
            n -= half;
        }
        if (!count_ || *lo != id)
            return {};
        const PackEntry& e = entries_[lo - ids_];
        return {base_ + e.offset, e.size, e.type};
    }

private:
    const char* base_ = nullptr;
    const uint64_t* ids_ = nullptr;
    const PackEntry* entries_ = nullptr;
    uint32_t count_ = 0;
    void* file_ = nullptr;
    void* mapping_ = nullptr;
};

}
