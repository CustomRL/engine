#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace engine::gpu {

class Device {
public:
    explicit Device(void* hwnd);

    // Binds and clears the back buffer, resizing it to the window first. False while the window has no area.
    bool beginFrame(int width, int height, const float clearColor[4]);
    void present(bool vsync);

    ID3D11Device* d3d() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }

private:
    void resize(int width, int height);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBuffer_;
    int width_ = 0;
    int height_ = 0;
};

}
