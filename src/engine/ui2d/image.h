#pragma once

#include "engine/gpu/texture.h"

#include <cstddef>
#include <filesystem>

namespace engine::ui2d {

// A PNG as a premultiplied RGBA texture with its mip chain, as Canvas samples it
gpu::Texture loadImage(gpu::Device& device, const std::filesystem::path& file);
gpu::Texture loadImage(gpu::Device& device, const void* data, size_t bytes);

}
