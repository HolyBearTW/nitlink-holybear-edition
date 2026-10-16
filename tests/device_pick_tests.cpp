// CPU tests for DeviceEnumerator::PickPreferredDevice: a saved device wins, capture cards come before
// cameras, and a PC with only cameras still opens one.
#include "capture/device_enumerator.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

using NitLink::DeviceEnumerator;
using NitLink::DeviceInfo;

static int checks = 0;
static void Require(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

static int Pick(std::initializer_list<const wchar_t*> names, const wchar_t* preferred = L"") {
    std::vector<DeviceInfo> devices;
    uint32_t index = 0;
    for (const wchar_t* name : names) {
        DeviceInfo d;
        d.name = name;
        d.index = index++;
        devices.push_back(d);
    }
    return DeviceEnumerator::PickPreferredDevice(devices, preferred);
}

int main() {
    Require(Pick({}) == -1, "no devices: -1");

    // The saved device always wins, even when it is a camera.
    Require(Pick({L"Integrated Camera", L"Elgato 4K Pro"}, L"Integrated Camera") == 0, "a saved camera is honoured");
    Require(Pick({L"Integrated Camera", L"Elgato 4K Pro"}, L"4k pro") == 1, "a saved short name matches by substring");

    // Cameras first in enumeration order are skipped for a capture card.
    Require(Pick({L"Integrated Webcam", L"Elgato 4K Pro"}) == 1, "laptop webcam skipped for an Elgato card");
    Require(Pick({L"Rift S Sensor", L"AVerMedia Live Gamer ULTRA S"}) == 1, "VR headset camera skipped for an AVerMedia card");
    Require(Pick({L"eTronVideo", L"UGREEN 25854"}) == 1, "Valve Index camera skipped for a UGREEN card");
    Require(Pick({L"OBS Virtual Camera", L"Cam Link 4K"}) == 1, "virtual camera skipped, Cam Link picked");
    Require(Pick({L"Elgato Facecam", L"Elgato 4K S"}) == 1, "an Elgato webcam is not a capture card");
    Require(Pick({L"Integrated Webcam", L"USB3.0 HD Video Capture"}) == 1, "generic capture card recognised by name");

    // Unknown names: the first one that does not look like a camera.
    Require(Pick({L"HTC Vive", L"USB Video"}) == 1, "unknown device preferred over a headset camera");
    Require(Pick({L"USB Video", L"Integrated Camera"}) == 0, "unknown device first stays first");

    // Among capture cards, Elgato keeps its priority; otherwise enumeration order.
    Require(Pick({L"AVerMedia GC553Pro", L"Elgato HD60 X"}) == 1, "Elgato before other capture cards");
    Require(Pick({L"AVerMedia GC553Pro", L"UGREEN 25854"}) == 0, "other capture cards in enumeration order");

    // Only cameras: the first one, so a PC without a capture card still shows a picture.
    Require(Pick({L"Integrated Camera", L"IR Camera"}) == 0, "cameras only: the first device");

    std::printf("{\"status\":\"DEVICE_PICK_PASS\",\"checks\":%d}\n", checks);
    return 0;
}
