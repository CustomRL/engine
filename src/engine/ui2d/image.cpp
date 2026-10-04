#include "engine/ui2d/image.h"

#include "engine/core/fatal.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace engine::ui2d {

gpu::Texture loadImage(gpu::Device& device, const void* data, size_t bytes) {
    int width, height, channels;
    stbi_uc* texels = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(bytes), &width, &height, &channels, 4);
    if (!texels)
        fatal("image decode: %s", stbi_failure_reason());
    for (int i = 0; i < width * height; i++) {
        stbi_uc* t = texels + i * 4;
        for (int k = 0; k < 3; k++)
            t[k] = static_cast<stbi_uc>((t[k] * t[3] + 127) / 255);
    }
    gpu::Texture out = gpu::createTexture2D(device, width, height, gpu::TextureFormat::Rgba8, texels, true);
    stbi_image_free(texels);
    return out;
}

gpu::Texture loadImage(gpu::Device& device, const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        fatal("no image %s", file.string().c_str());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), {}};
    return loadImage(device, bytes.data(), bytes.size());
}

}
