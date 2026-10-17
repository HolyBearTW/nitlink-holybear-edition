#include "renderer/dx11_renderer.h"
#include <dxgi1_2.h>
#include <iostream>
#include <stdexcept>

namespace NitLink {
struct DX11RendererTestAccess {
    static void Initialize(DX11Renderer& renderer, HWND hwnd, UINT width, UINT height)
    {
        // A hidden window and a WARP swap chain with the product's swap chain
        // description: no capture card, visible window, monitor mode change or
        // driver profile is involved in this regression test.
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &renderer.m_device, nullptr, &renderer.m_context))) {
            throw std::runtime_error("cannot create WARP fixture");
        }
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        if (FAILED(renderer.m_device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
            throw std::runtime_error("cannot reach the WARP DXGI factory");
        }
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.Width = width;
        scd.Height = height;
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc = {1, 0};
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (FAILED(factory->CreateSwapChainForHwnd(renderer.m_device.Get(), hwnd, &scd, nullptr, nullptr,
            &renderer.m_swapChain))) {
            throw std::runtime_error("cannot create the WARP swap chain fixture");
        }
        renderer.m_hwnd = hwnd;
        renderer.m_windowWidth = width;
        renderer.m_windowHeight = height;
        if (!renderer.CreateRenderTarget()) throw std::runtime_error("cannot create the fixture RTV");
    }
    static bool HasRenderTarget(const DX11Renderer& renderer) { return renderer.m_rtv != nullptr; }
    static IDXGISwapChain1* SwapChain(const DX11Renderer& renderer) { return renderer.m_swapChain.Get(); }
    static DXGI_FORMAT SwapFormat(const DX11Renderer& renderer) { return renderer.m_currentSwapFormat; }
    static bool SetSwapFormat(DX11Renderer& renderer, DXGI_FORMAT format)
    {
        return renderer.RecreateSwapChainBuffer(format);
    }
};
}

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void RequireBufferSize(IDXGISwapChain1* swapChain, UINT width, UINT height, const char* message)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    Require(SUCCEEDED(swapChain->GetDesc1(&desc)) && desc.Width == width && desc.Height == height, message);
}
}

int main()
{
    using namespace NitLink;
    using Access = DX11RendererTestAccess;
    try {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"NitLinkResizeRecoveryTest";
        RegisterClassW(&wc);
        HWND hwnd = CreateWindowW(wc.lpszClassName, L"resize recovery", WS_OVERLAPPEDWINDOW,
                                  0, 0, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
        Require(hwnd != nullptr, "cannot create the hidden fixture window");
        {
            DX11Renderer renderer;
            Access::Initialize(renderer, hwnd, 640, 360);

            // An outside reference to a back buffer, as an injected overlay can
            // hold across the application's resize, makes DXGI refuse it.
            Microsoft::WRL::ComPtr<ID3D11Texture2D> heldBackBuffer;
            Require(SUCCEEDED(Access::SwapChain(renderer)->GetBuffer(0, IID_PPV_ARGS(&heldBackBuffer))),
                    "cannot take the outside back-buffer reference");

            Require(!renderer.Resize(1280, 720), "a refused ResizeBuffers is reported to the caller");
            Require(Access::HasRenderTarget(renderer),
                    "a refused resize keeps a render target on the previous buffers");
            Require(renderer.GetWindowWidth() == 640 && renderer.GetWindowHeight() == 360,
                    "a refused resize keeps the previous size so the retry is not skipped");
            RequireBufferSize(Access::SwapChain(renderer), 640, 360, "the previous buffers stay in place");

            Require(!Access::SetSwapFormat(renderer, DXGI_FORMAT_R10G10B10A2_UNORM),
                    "a refused HDR format switch is reported to the caller");
            Require(Access::HasRenderTarget(renderer), "a refused format switch keeps a render target");
            Require(Access::SwapFormat(renderer) == DXGI_FORMAT_B8G8R8A8_UNORM,
                    "a refused format switch keeps the previous format");

            heldBackBuffer.Reset();
            Require(renderer.Resize(1280, 720), "the retry succeeds once the outside reference is gone");
            Require(Access::HasRenderTarget(renderer), "a successful resize recreates the render target");
            Require(renderer.GetWindowWidth() == 1280 && renderer.GetWindowHeight() == 720,
                    "a successful resize records the new size");
            RequireBufferSize(Access::SwapChain(renderer), 1280, 720, "the buffers take the new size");

            Require(Access::SetSwapFormat(renderer, DXGI_FORMAT_R10G10B10A2_UNORM),
                    "the HDR format switch succeeds once the outside reference is gone");
            Require(Access::HasRenderTarget(renderer), "a successful format switch recreates the render target");
        }
        DestroyWindow(hwnd);
        std::cout << "PASS refused swap chain resize and format switch keep rendering and retry\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
