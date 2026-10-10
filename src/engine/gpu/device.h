#pragma once

#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <cstdint>

namespace engine::gpu {

class Device {
public:
    // A device presenting to the window
    explicit Device(void* hwnd);
    // A device presenting to the window with a windowless device's D3D device and context, so the D3D device can be
    // made on another thread while the window is made
    Device(void* hwnd, Device&& windowless);
    // A device with no window, for drawing offscreen only: beginFrame and present are not for it
    Device();

    // Binds and clears the back buffer and its depth buffer (D32_FLOAT, cleared to 1), resizing them to the window first, and sets the
    // viewport to cover them. False while the window has no area.
    bool beginFrame(int width, int height, const float clearColor[4]);
    // With vsync, waits for the display's refresh. Without, returns at once, so the frame rate is unlimited: the frame tears onto the
    // screen where the system allows tearing, else the compositor shows the newest frame at its next refresh.
    void present(bool vsync);

    ID3D11Device* d3d() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    // The window's back buffer, at the size the last beginFrame gave it
    ID3D11RenderTargetView* backBuffer() const { return backBuffer_.Get(); }
    // Bytes of the adapter's local (video) memory this process uses now, as DXGI reports it; 0 where DXGI can't tell
    uint64_t videoMemoryUsage() const;

private:
    void createDevice();
    void createSwapChain(void* hwnd);
    void resize(int width, int height);
    UINT swapChainFlags() const;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBuffer_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_;
    int width_ = 0;
    int height_ = 0;
    bool tearing_ = false;
};

}
