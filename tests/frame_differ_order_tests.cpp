#include "capture/frame_differ.h"

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
unsigned checks = 0;
void Require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}

struct CpuGraphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Query> complete;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> inputs;

    CpuGraphics() {
        D3D_FEATURE_LEVEL level{};
        Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context)), "WARP device creation succeeds");
        D3D11_QUERY_DESC query{};
        query.Query = D3D11_QUERY_EVENT;
        Require(SUCCEEDED(device->CreateQuery(&query, &complete)), "completion query creation succeeds");
        for (size_t color = 0; color < inputs.size(); ++color) {
            std::array<uint32_t, 64> pixels{};
            pixels.fill(color ? 0xffffffffu : 0xff000000u);
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = desc.Height = 8;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = pixels.data();
            initial.SysMemPitch = 8 * sizeof(uint32_t);
            ComPtr<ID3D11Texture2D> texture;
            Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)) &&
                    SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &inputs[color])),
                    "known source image creation succeeds");
        }
    }

    void Finish() {
        context->End(complete.Get());
        context->Flush();
        for (unsigned attempt = 0; attempt < 2000; ++attempt) {
            const HRESULT result = context->GetData(complete.Get(), nullptr, 0, 0);
            if (result == S_OK) return;
            Require(result == S_FALSE, "WARP completion query remains valid");
            Sleep(1);
        }
        throw std::runtime_error("WARP comparison commands did not finish");
    }
};

size_t Color(uint64_t frame, unsigned cadence) {
    return cadence == 0 ? 0 : static_cast<size_t>(((frame - 1) / cadence) % 2);
}

void RunPattern(CpuGraphics& cpu, unsigned cadence, unsigned frames, bool synchronous) {
    NitLink::FrameDiffer differ;
    Require(differ.Initialize(cpu.device.Get()), "production differ initializes on WARP");
    uint64_t lastMeasured = 0;
    unsigned measured = 0;
    for (uint64_t frame = 1; frame <= frames + 8; ++frame) {
        differ.Process(cpu.context.Get(), cpu.inputs[Color(frame, cadence)].Get());
        if (differ.HasFrameClassification()) {
            const uint64_t compared = differ.GetClassifiedFrameIndex();
            Require(compared > lastMeasured && compared >= 2 && compared < frame,
                    "production readbacks advance once in source order and identify an earlier frame");
            const bool gap = lastMeasured != 0 && compared != lastMeasured + 1;
            Require(differ.HasClassificationDiscontinuity() == gap, "production discontinuities match missing source indices");
            const bool expected = Color(compared, cadence) != Color(compared - 1, cadence);
            Require(differ.WasPreviousFrameNew() == expected,
                    "production classifier matches the known comparison's actual source images");
            if (synchronous)
                Require(compared == frame - 1 && !gap, "ready comparisons retain one-frame readback latency");
            lastMeasured = compared;
            ++measured;
        } else {
            Require(differ.GetClassifiedFrameIndex() == 0 && !differ.HasClassificationDiscontinuity(),
                    "unavailable readbacks expose no repeated frame identity");
        }
        if (synchronous || frame > frames) cpu.Finish();
    }
    Require(measured >= (synchronous ? frames : 8u), "production readbacks make progress");
    cpu.Finish();
    cpu.context->ClearState();
}

void CheckReset(CpuGraphics& cpu) {
    NitLink::FrameDiffer differ;
    Require(differ.Initialize(cpu.device.Get()), "reset test initializes the differ");
    for (uint64_t frame = 1; frame <= 12; ++frame)
        differ.Process(cpu.context.Get(), cpu.inputs[Color(frame, 1)].Get());
    differ.Reset();
    for (uint64_t frame = 1; frame <= 5; ++frame) {
        differ.Process(cpu.context.Get(), cpu.inputs[0].Get());
        if (frame <= 2) {
            Require(!differ.HasFrameClassification() && differ.WasPreviousFrameNew(),
                    "stream reset cannot classify a queued comparison from the old stream");
        } else {
            Require(differ.HasFrameClassification() && differ.GetClassifiedFrameIndex() == frame - 1 &&
                    !differ.HasClassificationDiscontinuity() && !differ.WasPreviousFrameNew(),
                    "new-stream static results have new-stream identities and content");
        }
        cpu.Finish();
    }
    differ.Shutdown();
    Require(!differ.HasFrameClassification(), "shutdown exposes no old result");
    Require(differ.Initialize(cpu.device.Get()), "the differ can initialize again after shutdown");
    differ.Process(cpu.context.Get(), cpu.inputs[0].Get());
    Require(!differ.HasFrameClassification() && differ.WasPreviousFrameNew(),
            "reinitialization starts a fresh comparison stream");
    cpu.Finish();
    cpu.context->ClearState();
}
}

int main() {
    try {
        CpuGraphics cpu;
        RunPattern(cpu, 2, 120, true);
        RunPattern(cpu, 1, 120, true);
        RunPattern(cpu, 0, 120, true);
        RunPattern(cpu, 2, 400, false);
        CheckReset(cpu);
        std::cout << "PASS WARP ordered production comparisons, 30/60/static source images, asynchronous pressure and reset; checks=" << checks << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
