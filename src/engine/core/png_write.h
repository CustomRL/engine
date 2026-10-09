#pragma once

#include <cstdint>
#include <filesystem>

namespace engine {

// An 8-bit RGBA image written as a PNG with stored (uncompressed) deflate blocks: big files, no dependency. rgba: width * height * 4
// bytes, top row first. False if the file can't be written.
bool writePng(const std::filesystem::path& path, int width, int height, const uint8_t* rgba);

}
