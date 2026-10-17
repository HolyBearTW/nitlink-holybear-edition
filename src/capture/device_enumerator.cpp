#include "device_enumerator.h"
#include <mfapi.h>
#include <mfidl.h>
#include <windows.h>
#include <debugapi.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <sstream>

namespace NitLink {

namespace {

void DebugLog(const std::wstring& msg)
{
    OutputDebugStringW((L"[NitLink/DeviceEnum] " + msg + L"\n").c_str());
}

// Lowercase a wide string in place using the Windows locale-aware helper.
// Matches the convention used elsewhere in the capture layer
// (elgato_hid_4ks.cpp's PathContainsCaseInsensitive) for consistency.
std::wstring ToLower(std::wstring s)
{
    if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

// Lowercase name fragments of capture cards. The automatic pick prefers these.
const wchar_t* const kCaptureCardNames[] = {
    L"elgato", L"avermedia", L"live gamer", L"magewell", L"blackmagic", L"cam link",
    L"razer ripsaw", L"evga xr1", L"startech", L"ugreen", L"capture", L"hdmi",
};

// Lowercase name fragments of cameras. Webcams, VR headset cameras and virtual
// cameras enumerate as video capture devices too; the automatic pick skips them
// while any other device is present.
const wchar_t* const kCameraNames[] = {
    L"camera", L"webcam", L"facecam", L"facetime", L"windows hello", L"brio",
    L"c920", L"c922", L"c930", L"streamcam", L"logitech hd", L"oculus", L"rift",
    L"quest", L"vive", L"etronvideo", L"pico", L"nvidia broadcast",
};

template <size_t N>
bool NameContainsAny(const std::wstring& name, const wchar_t* const (&fragments)[N])
{
    const std::wstring lower = ToLower(name);
    for (const wchar_t* fragment : fragments) {
        if (lower.find(fragment) != std::wstring::npos) return true;
    }
    return false;
}

bool LooksLikeCamera(const std::wstring& name) { return NameContainsAny(name, kCameraNames); }
bool LooksLikeCaptureCard(const std::wstring& name) { return NameContainsAny(name, kCaptureCardNames); }

// Single Media Foundation enumeration pass. Split out from the public
// FindCaptureDevices entry point so the wrapper can retry on a
// transiently empty result without duplicating the COM walk.
std::vector<DeviceInfo> EnumerateOnce()
{
    std::vector<DeviceInfo> result;

    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 1);
    if (FAILED(hr)) return result;

    hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) return result;

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attrs.Get(), &devices, &count);
    if (FAILED(hr)) return result;

    for (UINT32 i = 0; i < count; i++) {
        DeviceInfo info;
        info.index = i;

        // Get friendly name
        WCHAR* name = nullptr;
        UINT32 nameLen = 0;
        hr = devices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen);
        if (SUCCEEDED(hr) && name) {
            info.name = name;
            CoTaskMemFree(name);
        }

        // Get symbolic link
        WCHAR* link = nullptr;
        UINT32 linkLen = 0;
        hr = devices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &linkLen);
        if (SUCCEEDED(hr) && link) {
            info.symbolicLink = link;
            CoTaskMemFree(link);
        }

        result.push_back(std::move(info));
        devices[i]->Release();
    }
    CoTaskMemFree(devices);

    return result;
}

} // anonymous namespace


std::vector<DeviceInfo> DeviceEnumerator::FindCaptureDevices()
{
    // Media Foundation can return an empty enumeration during brief
    // windows: a Device Manager disable/enable cycle on a neighboring
    // device, a capture-card driver service still spinning up after a
    // cold boot, or the device being briefly claimed and released by
    // another process. Retry a small number of times before treating
    // an empty result as final. Each attempt is cheap (a few COM calls
    // and a friendly-name read per device); three attempts at 500 ms
    // spacing covers the common transient windows without adding
    // noticeable startup latency in the steady state.
    constexpr int kMaxAttempts = 3;
    constexpr auto kRetryDelay = std::chrono::milliseconds(500);

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        auto result = EnumerateOnce();
        if (!result.empty() || attempt == kMaxAttempts - 1) {
            if (attempt > 0) {
                std::wstringstream ss;
                ss << L"Enumeration succeeded on attempt " << (attempt + 1)
                   << L" with " << result.size() << L" device(s)";
                DebugLog(ss.str());
            }
            return result;
        }
        DebugLog(L"Enumeration returned 0 devices, retrying");
        std::this_thread::sleep_for(kRetryDelay);
    }
    return {};
}

int DeviceEnumerator::PickPreferredDevice(const std::vector<DeviceInfo>& devices,
                                          const std::wstring& preferredName)
{
    if (devices.empty()) return -1;

    if (!preferredName.empty()) {
        const std::wstring needle = ToLower(preferredName);

        // Strategy 1: case-insensitive exact name match.
        for (size_t i = 0; i < devices.size(); ++i) {
            if (ToLower(devices[i].name) == needle) {
                return static_cast<int>(i);
            }
        }

        // Strategy 2: case-insensitive substring match. Allows a saved
        // short identifier like "Elgato 4K Pro" to match a longer
        // enumerated name when drivers append suffixes or instance
        // numbers.
        for (size_t i = 0; i < devices.size(); ++i) {
            if (ToLower(devices[i].name).find(needle) != std::wstring::npos) {
                return static_cast<int>(i);
            }
        }
    }

    // Strategy 3: first Elgato device that is not a camera, then the first
    // other capture card by name that is not a camera. A webcam, a VR
    // headset camera or a virtual camera tends to win MF enumeration order
    // and would otherwise be opened by default.
    for (size_t i = 0; i < devices.size(); ++i) {
        const std::wstring& name = devices[i].name;
        if (ToLower(name).find(L"elgato") != std::wstring::npos && !LooksLikeCamera(name)) {
            return static_cast<int>(i);
        }
    }
    for (size_t i = 0; i < devices.size(); ++i) {
        if (LooksLikeCaptureCard(devices[i].name) && !LooksLikeCamera(devices[i].name)) {
            return static_cast<int>(i);
        }
    }

    // Strategy 4: first device that does not look like a camera (a capture
    // card with an unrecognized name).
    for (size_t i = 0; i < devices.size(); ++i) {
        if (!LooksLikeCamera(devices[i].name)) return static_cast<int>(i);
    }

    // Strategy 5: every device looks like a camera; the first one in MF
    // enumeration order.
    return 0;
}

std::vector<DeviceInfo> DeviceEnumerator::FindCaptureCards()
{
    auto all = FindCaptureDevices();

    std::vector<DeviceInfo> filtered;
    for (const auto& dev : all) {
        if (!LooksLikeCamera(dev.name)) filtered.push_back(dev);
    }

    // If filtering removed everything, return all devices
    // (user might have an unusual capture card)
    return filtered.empty() ? all : filtered;
}

int DeviceEnumerator::FindDeviceByName(const std::vector<DeviceInfo>& devices,
                                       const std::wstring& name)
{
    if (devices.empty() || name.empty()) return -1;

    const std::wstring needle = ToLower(name);

    // Strategy 1: case-insensitive exact name match.
    for (size_t i = 0; i < devices.size(); ++i) {
        if (ToLower(devices[i].name) == needle) {
            return static_cast<int>(i);
        }
    }

    // Strategy 2: case-insensitive substring match. Handles the case
    // where the saved or user-supplied name is a short identifier and
    // the enumerated friendly name has extra suffixes such as a
    // driver-instance number.
    for (size_t i = 0; i < devices.size(); ++i) {
        if (ToLower(devices[i].name).find(needle) != std::wstring::npos) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

} // namespace NitLink
