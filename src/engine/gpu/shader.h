#pragma once

#include "engine/gpu/device.h"

#include <d3dcommon.h>

#include <cstdint>
#include <string>
#include <vector>

namespace engine::gpu {

// One element of a vertex shader's input signature
struct ShaderInput {
    std::string semantic;
    int index = 0;
    int reg = 0;
};

// A texture, sampler or constant buffer a shader binds, by its name in the bytecode
struct ShaderBinding {
    std::string name;
    D3D_SHADER_INPUT_TYPE type;
    int point = 0;
    D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_UNKNOWN;
};

// A compiled shader created from its bytecode, with what reflection tells about it
struct ShaderInfo {
    std::vector<uint8_t> code;
    std::vector<ShaderInput> inputs;      // vertex shaders
    std::vector<ShaderBinding> bindings;
    std::vector<int> constantBufferSizes; // bytes, by register
};

struct VertexShader : ShaderInfo {
    Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
};

struct PixelShader : ShaderInfo {
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
};

VertexShader createVertexShader(Device& device, std::vector<uint8_t> code);
PixelShader createPixelShader(Device& device, std::vector<uint8_t> code);

// An input layout for a vertex shader's signature
Microsoft::WRL::ComPtr<ID3D11InputLayout> createInputLayout(Device& device, const VertexShader& shader,
                                                            const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements);

}
