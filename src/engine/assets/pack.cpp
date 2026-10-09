#include "engine/assets/pack.h"

#include <windows.h>

namespace engine::assets {

bool Pack::open(const wchar_t* path, const char** error) {
    close();
    const char* why = nullptr;
    LARGE_INTEGER size{};
    file_ = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        file_ = nullptr;
        why = "can't open the file";
    } else if (!GetFileSizeEx(file_, &size) || size.QuadPart < static_cast<LONGLONG>(sizeof(PackHeader))) {
        why = "file smaller than a pack header";
    } else if (!(mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr))) {
        why = "CreateFileMapping failed";
    } else if (!(base_ = static_cast<const char*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0)))) {
        why = "MapViewOfFile failed";
    } else {
        const PackHeader& h = *header();
        const uint64_t bytes = static_cast<uint64_t>(size.QuadPart);
        const uint64_t table = sizeof(PackHeader) + uint64_t(h.count) * (sizeof(uint64_t) + sizeof(PackEntry));
        if (h.magic != kPackMagic)
            why = "not a pack (magic)";
        else if (h.version != kPackVersion)
            why = "unknown pack version";
        else if (h.fileSize != bytes || bytes % kPackAlign)
            why = "file size disagrees with the header";
        else if (h.blobStart % kPackAlign || h.blobStart < table || h.blobStart > bytes)
            why = "table runs into the blobs";
        else {
            ids_ = reinterpret_cast<const uint64_t*>(base_ + sizeof(PackHeader));
            entries_ = reinterpret_cast<const PackEntry*>(ids_ + h.count);
            for (uint32_t i = 0; i < h.count && !why; i++) {
                const PackEntry& e = entries_[i];
                if (i && ids_[i] <= ids_[i - 1])
                    why = "ids not strictly ascending";
                else if (e.offset % kPackAlign || e.offset < h.blobStart || e.offset > bytes || e.size > bytes - e.offset)
                    why = "blob out of bounds";
            }
            count_ = h.count;
        }
    }
    if (why) {
        close();
        if (error)
            *error = why;
        return false;
    }
    return true;
}

void Pack::close() {
    if (base_)
        UnmapViewOfFile(base_);
    if (mapping_)
        CloseHandle(mapping_);
    if (file_)
        CloseHandle(file_);
    base_ = nullptr;
    mapping_ = file_ = nullptr;
    ids_ = nullptr;
    entries_ = nullptr;
    count_ = 0;
}

}
