#include "capture/frame_differ.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
unsigned checks = 0;
void Require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Query> completion;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> inputs;
    Graphics() {
        D3D_FEATURE_LEVEL level{};
        Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context)), "WARP device creation succeeds");
        D3D11_QUERY_DESC query{}; query.Query = D3D11_QUERY_EVENT;
        Require(SUCCEEDED(device->CreateQuery(&query, &completion)), "completion query creation succeeds");
        for (size_t i = 0; i < inputs.size(); ++i) {
            std::array<uint32_t, 64> pixels{};
            pixels.fill(i ? 0xffffffffu : 0xff000000u);
            D3D11_TEXTURE2D_DESC d{};
            d.Width = d.Height = 8; d.MipLevels = d.ArraySize = 1;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = pixels.data(); data.SysMemPitch = 32;
            ComPtr<ID3D11Texture2D> texture;
            Require(SUCCEEDED(device->CreateTexture2D(&d, &data, &texture)) &&
                SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &inputs[i])), "source images exist");
        }
    }
    void Finish() {
        context->End(completion.Get()); context->Flush();
        for (unsigned i = 0; i < 2000; ++i) {
            const auto hr = context->GetData(completion.Get(), nullptr, 0, 0);
            if (hr == S_OK) return;
            Require(hr == S_FALSE, "completion query remains valid"); Sleep(1);
        }
        throw std::runtime_error("WARP commands did not finish");
    }
};
size_t Color(uint64_t frame, unsigned cadence) {
    return cadence ? static_cast<size_t>(((frame - 1) / cadence) % 2) : 0;
}
void Pattern(Graphics& gpu, unsigned cadence) {
    NitLink::FrameDiffer differ;
    Require(differ.Initialize(gpu.device.Get()), "production differ initializes");
    differ.SetReadbackWaitAllowed(true);
    unsigned known = 0, unknown = 0;
    std::vector<uint64_t> presents;
    std::vector<double> callMs;
    for (uint64_t frame = 1; frame <= 180; ++frame) {
        const auto begin = std::chrono::steady_clock::now();
        differ.Process(gpu.context.Get(), gpu.inputs[Color(frame, cadence)].Get());
        callMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        const bool present = differ.ShouldPresentCurrentFrame();
        if (differ.HasFrameClassification()) {
            Require(differ.GetClassifiedFrameIndex() == frame,
                "Source pacing must classify the picture being selected, never an earlier captured picture");
            const bool changed = Color(frame, cadence) != Color(frame - 1, cadence);
            Require(differ.WasPreviousFrameNew() == changed, "the current picture verdict matches known source images");
            Require(present == changed, "presentation selects the actual changed source picture");
            ++known;
        } else {
            Require(present, "an unavailable comparison cannot suppress the latest captured picture");
            if (frame > 1) ++unknown;
        }
        if (present) presents.push_back(frame);
        gpu.Finish();
    }
    Require(known >= 150, "ready WARP comparisons make current-picture progress");
    if (!unknown) {
        if (cadence == 0) Require(presents.size() == 1, "static content needs only its initial presentation before cadence holds");
        else {
            for (size_t i = 1; i < presents.size(); ++i) {
                Require(presents[i] - presents[i - 1] == cadence, "known source content has even submission spacing");
                Require((presents[i] - 1) / cadence > (presents[i - 1] - 1) / cadence,
                    "each presentation advances its source picture identity without repeat or skipped content");
            }
        }
    }
    double total = 0, maximum = 0;
    for (double ms : callMs) { total += ms; if (ms > maximum) maximum = ms; }
    std::cout << "cadence=" << cadence << " known=" << known << " unknown=" << unknown
        << " waitSuccess=" << differ.GetReadbackWaits() << " waitTimeout=" << differ.GetReadbackWaitTimeouts()
        << " processMeanMs=" << total / callMs.size() << " processMaxMs=" << maximum << '\n';
    gpu.context->ClearState();
}
void ModesAndReset(Graphics& gpu) {
    NitLink::FrameDiffer differ;
    Require(differ.Initialize(gpu.device.Get()), "mode-switch differ initializes");
    for (uint64_t frame = 1; frame <= 24; ++frame)
        differ.Process(gpu.context.Get(), gpu.inputs[Color(frame, 2)].Get());
    gpu.Finish();
    differ.SetReadbackWaitAllowed(true);
    for (uint64_t frame = 25; frame <= 48; ++frame) {
        differ.Process(gpu.context.Get(), gpu.inputs[Color(frame, 2)].Get());
        Require(!differ.HasFrameClassification() || differ.GetClassifiedFrameIndex() == frame,
            "enabling Source pacing cannot reuse the asynchronous backlog for presentation");
        gpu.Finish();
    }
    differ.Reset();
    differ.Process(gpu.context.Get(), gpu.inputs[0].Get());
    Require(!differ.HasFrameClassification() && differ.WasPreviousFrameNew(), "reset exposes no old-stream result");
    gpu.Finish();
    differ.Process(gpu.context.Get(), gpu.inputs[0].Get());
    Require(!differ.HasFrameClassification() || (differ.GetClassifiedFrameIndex() == 2 && !differ.WasPreviousFrameNew()),
        "new-stream static comparison belongs to its own current picture");
    differ.Process(gpu.context.Get(), nullptr);
    Require(!differ.HasFrameClassification() && differ.ShouldPresentCurrentFrame(),
        "an invalidated static verdict cannot suppress a newly available picture");
    differ.SetReadbackWaitAllowed(false);
    gpu.Finish();
    for (uint64_t frame = 3; frame <= 10; ++frame) {
        differ.Process(gpu.context.Get(), gpu.inputs[Color(frame, 1)].Get());
        Require(!differ.HasFrameClassification() || differ.GetClassifiedFrameIndex() < frame,
            "capture and refresh pacing retain asynchronous telemetry");
        gpu.Finish();
    }
    gpu.context->ClearState();
}
}
int main() {
    try {
        Graphics gpu;
        Pattern(gpu, 2); Pattern(gpu, 1); Pattern(gpu, 0); ModesAndReset(gpu);
        std::cout << "PASS current picture identities, 30/60/static spacing, backlog mode switch and reset; checks=" << checks << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}