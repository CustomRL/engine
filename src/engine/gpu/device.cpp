#include "engine/gpu/device.h"

#include "engine/core/fatal.h"

using Microsoft::WRL::ComPtr;

namespace engine::gpu {

namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

HRESULT makeDevice(UINT flags, ID3D11Device** device, ID3D11DeviceContext** context) {
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &level, 1, D3D11_SDK_VERSION, device, nullptr,
                             context);
}

}

Device::Device() {
    createDevice();
}

void Device::createDevice() {
#ifdef NDEBUG
    check(makeDevice(0, &device_, &context_), "D3D11CreateDevice");
#else
    HRESULT hr = makeDevice(D3D11_CREATE_DEVICE_DEBUG, &device_, &context_);
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) // the Graphics Tools feature that holds the debug layer is not installed
        hr = makeDevice(0, &device_, &context_);
    check(hr, "D3D11CreateDevice");
#endif
}

Device::Device(void* hwnd) {
    createDevice();

    ComPtr<IDXGIDevice> dxgiDevice;
    check(device_.As(&dxgiDevice), "IDXGIDevice");
    ComPtr<IDXGIAdapter> adapter;
    check(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
    ComPtr<IDXGIFactory2> factory;
    check(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIFactory2");
    // Unsynced presents tear instead of waiting for the compositor where the system allows it (Windows 10 1803 and a driver that agrees)
    ComPtr<IDXGIFactory5> factory5;
    BOOL tearing = FALSE;
    tearing_ = SUCCEEDED(factory.As(&factory5)) &&
               SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing)) && tearing;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = swapChainFlags();
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
    context_->OMSetRenderTargets(1, &target, depth_.Get());
    context_->ClearRenderTargetView(target, clearColor);
    context_->ClearDepthStencilView(depth_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    context_->RSSetViewports(1, &viewport);
    return true;
}

void Device::present(bool vsync) {
    check(swapChain_->Present(vsync ? 1 : 0, !vsync && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0), "Present");
}

uint64_t Device::videoMemoryUsage() const {
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIAdapter3> adapter3;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(device_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter.As(&adapter3)) ||
        FAILED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        return 0;
    return info.CurrentUsage;
}

UINT Device::swapChainFlags() const {
    return tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
}

void Device::resize(int width, int height) {
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    backBuffer_.Reset();
    depth_.Reset();
    check(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapChainFlags()), "ResizeBuffers");
    ComPtr<ID3D11Texture2D> texture;
    check(swapChain_->GetBuffer(0, IID_PPV_ARGS(&texture)), "GetBuffer");
    check(device_->CreateRenderTargetView(texture.Get(), nullptr, &backBuffer_), "CreateRenderTargetView");

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTexture;
    check(device_->CreateTexture2D(&depthDesc, nullptr, &depthTexture), "CreateTexture2D (depth)");
    check(device_->CreateDepthStencilView(depthTexture.Get(), nullptr, &depth_), "CreateDepthStencilView");
    width_ = width;
    height_ = height;
}

}
