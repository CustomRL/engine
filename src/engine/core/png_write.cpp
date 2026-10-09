#include "engine/core/png_write.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace engine {

namespace {

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; i++)
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

void be32(std::vector<uint8_t>& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8)
        out.push_back(static_cast<uint8_t>(v >> s));
}

void chunk(std::ofstream& file, const char* type, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    be32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    be32(out, crc32(out.data() + 4, body.size() + 4));
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

}

bool writePng(const std::filesystem::path& path, int width, int height, const uint8_t* rgba) {
    std::vector<uint8_t> header;
    be32(header, width);
    be32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});  // 8 bits, RGBA, deflate, adaptive filtering, no interlace

    // each row behind filter byte 0, in a zlib stream of stored blocks of at most 65535 bytes
    const size_t row = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (row + 1));
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + y * row, rgba + (y + 1) * row);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t at = 0; at < raw.size(); at += 65535) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    }
    uint32_t a = 1, b = 0;  // Adler-32
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);

    std::ofstream file(path, std::ios::binary);
    const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    file.write(reinterpret_cast<const char*>(signature), 8);
    chunk(file, "IHDR", header);
    chunk(file, "IDAT", z);
    chunk(file, "IEND", {});
    file.close();
    return file.good();
}

}
