#include "engine/gpu/device.h"

#include "engine/core/fatal.h"

using Microsoft::WRL::ComPtr;

namespace engine::gpu {

namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

HRESULT createDevice(UINT flags, ID3D11Device** device, ID3D11DeviceContext** context) {
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &level, 1, D3D11_SDK_VERSION, device, nullptr,
                             context);
}

}

Device::Device(void* hwnd) {
#ifdef NDEBUG
    check(createDevice(0, &device_, &context_), "D3D11CreateDevice");
#else
    HRESULT hr = createDevice(D3D11_CREATE_DEVICE_DEBUG, &device_, &context_);
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) // the Graphics Tools feature that holds the debug layer is not installed
        hr = createDevice(0, &device_, &context_);
    check(hr, "D3D11CreateDevice");
#endif

    ComPtr<IDXGIDevice> dxgiDevice;
    check(device_.As(&dxgiDevice), "IDXGIDevice");
    ComPtr<IDXGIAdapter> adapter;
    check(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
    ComPtr<IDXGIFactory2> factory;
    check(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIFactory2");

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const auto window = static_cast<HWND>(hwnd);
    check(factory->CreateSwapChainForHwnd(device_.Get(), window, &desc, nullptr, nullptr, &swapChain_), "CreateSwapChainForHwnd");
    // SDL owns fullscreen; DXGI's own Alt+Enter would fight it.
    check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
}

bool Device::beginFrame(int width, int height, const float clearColor[4]) {
    if (width <= 0 || height <= 0)
        return false;
    if (width != width_ || height != height_)
        resize(width, height);
    ID3D11RenderTargetView* target = backBuffer_.Get();
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->ClearRenderTargetView(target, clearColor);
    return true;
}

void Device::present(bool vsync) {
    check(swapChain_->Present(vsync ? 1 : 0, 0), "Present");
}

void Device::resize(int width, int height) {
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    backBuffer_.Reset();
    check(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
    ComPtr<ID3D11Texture2D> texture;
    check(swapChain_->GetBuffer(0, IID_PPV_ARGS(&texture)), "GetBuffer");
    check(device_->CreateRenderTargetView(texture.Get(), nullptr, &backBuffer_), "CreateRenderTargetView");
    width_ = width;
    height_ = height;
}

}
