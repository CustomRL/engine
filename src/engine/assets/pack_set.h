#pragma once

#include "engine/assets/pack.h"

namespace engine::assets {

// The packs of one cook, mapped together at startup, then any a run loads later (a menu's newly equipped items). The first pack's
// build pins the run: a pack cooked from another build is refused, so a run never mixes two cooks. find looks in each pack in turn;
// no allocation.
class PackSet {
public:
    static constexpr int kMaxPacks = 256;

    // Maps one more pack. False with a message in error (when given) if it can't be mapped, isn't a pack, comes from another build than
    // the packs before it, or there are already kMaxPacks.
    bool open(const wchar_t* path, const char** error = nullptr);
    void close();

    Blob find(uint64_t id) const {
        for (int i = 0; i < count_; i++)
            if (const Blob b = packs_[i].find(id); b.data)
                return b;
        return {};
    }
    const char* build() const { return build_; }  // "" until a pack is open
    int count() const { return count_; }
    const Pack& pack(int i) const { return packs_[i]; }

private:
    Pack packs_[kMaxPacks];
    int count_ = 0;
    char build_[sizeof(PackHeader::build) + 1] = {};
};

}
