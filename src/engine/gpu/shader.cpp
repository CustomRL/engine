#include "engine/gpu/shader.h"

#include "engine/core/fatal.h"

#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace engine::gpu {

namespace {

void reflect(ShaderInfo& info) {
    ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(info.code.data(), info.code.size(), IID_PPV_ARGS(&reflection))))
        fatal("D3DReflect failed: not D3D11 bytecode");
    D3D11_SHADER_DESC desc;
    reflection->GetDesc(&desc);
    for (UINT i = 0; i < desc.InputParameters; i++) {
        D3D11_SIGNATURE_PARAMETER_DESC p;
        reflection->GetInputParameterDesc(i, &p);
        if (p.SystemValueType == D3D_NAME_UNDEFINED)
            info.inputs.push_back({p.SemanticName, static_cast<int>(p.SemanticIndex), static_cast<int>(p.Register)});
    }
    for (UINT i = 0; i < desc.BoundResources; i++) {
        D3D11_SHADER_INPUT_BIND_DESC b;
        reflection->GetResourceBindingDesc(i, &b);
        info.bindings.push_back({b.Name, b.Type, static_cast<int>(b.BindPoint), b.Dimension});
        if (b.Type == D3D_SIT_CBUFFER) {
            D3D11_SHADER_BUFFER_DESC buffer;
            reflection->GetConstantBufferByName(b.Name)->GetDesc(&buffer);
            info.constantBufferSizes.resize(std::max<size_t>(info.constantBufferSizes.size(), b.BindPoint + 1));
            info.constantBufferSizes[b.BindPoint] = static_cast<int>(buffer.Size);
        }
    }
}

}

VertexShader createVertexShader(Device& device, std::vector<uint8_t> code) {
    VertexShader out;
    out.code = std::move(code);
    if (FAILED(device.d3d()->CreateVertexShader(out.code.data(), out.code.size(), nullptr, &out.shader)))
        fatal("CreateVertexShader failed");
    reflect(out);
    return out;
}

PixelShader createPixelShader(Device& device, std::vector<uint8_t> code) {
    PixelShader out;
    out.code = std::move(code);
    if (FAILED(device.d3d()->CreatePixelShader(out.code.data(), out.code.size(), nullptr, &out.shader)))
        fatal("CreatePixelShader failed");
    reflect(out);
    return out;
}

ComPtr<ID3D11InputLayout> createInputLayout(Device& device, const VertexShader& shader, const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements) {
    ComPtr<ID3D11InputLayout> layout;
    if (FAILED(device.d3d()->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), shader.code.data(), shader.code.size(),
                                               &layout)))
        fatal("CreateInputLayout failed");
    return layout;
}

}
