#include "engine/assets/pack_set.h"

#include <cstring>

namespace engine::assets {

bool PackSet::open(const wchar_t* path, const char** error) {
    const char* why = nullptr;
    if (count_ == kMaxPacks)
        why = "too many packs";
    else if (packs_[count_].open(path, &why) && count_ &&
             std::memcmp(packs_[count_].header()->build, build_, sizeof(PackHeader::build)) != 0) {
        packs_[count_].close();
        why = "pack cooked from another build than the run's";
    }
    if (why) {
        if (error)
            *error = why;
        return false;
    }
    if (!count_)
        std::memcpy(build_, packs_[0].header()->build, sizeof(PackHeader::build));
    count_++;
    return true;
}

void PackSet::close() {
    for (int i = 0; i < count_; i++)
        packs_[i].close();
    count_ = 0;
    build_[0] = 0;
}

}
