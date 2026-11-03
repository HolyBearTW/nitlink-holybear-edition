#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "game_service.h"
#include "input/shortcut_bindings.h"

namespace NitLink {

struct HolyBearUpdateSettingsState {
    std::wstring current;
    std::wstring latest;
    uint64_t lastCheckUtc = 0;
    bool automatic = false;
    bool busy = false;
    std::wstring status;
    bool hasNewer = false;
    bool unread = false;
    std::wstring url;
    std::wstring notes, publishedAt, transferStatus;
    uint64_t packageBytes = 0, receivedBytes = 0;
    bool canDownload = false;
};

struct ManualHdmiSourceOptionState {
    std::wstring value;
    std::wstring label;
};

// The state fields shared by the About panel, source pickers, and No Signal
// controls. Keeping their production serializer pure lets the WebView runtime
// test exercise the exact JSON that PushSettingsState sends.
struct WebViewCoreSettingsState {
    ShortcutBindings shortcutBindings=DefaultShortcutBindings;
    unsigned hudMode=0,compactHudColor=0x76b900,compactHudMetrics=15,compactHudTextSize=14;
    bool hudVisible=false;
    std::wstring languagePreference;
    std::wstring locale;
    HolyBearUpdateSettingsState holyBearUpdate;
    std::wstring manualHdmiSource;
    std::wstring manualHdmiSourceCustom;
    std::wstring effectiveHdmiSource;
    std::vector<ManualHdmiSourceOptionState> manualHdmiSourceOptions;
    bool preventSleep = false;
    std::wstring noSignalMode;
    std::wstring noSignalImage;
    bool noSignalImageAvailable = false;
    std::wstring noSignalFit;
    bool noSignalDimImage = true;
    std::vector<std::wstring> captureDevices;
    std::wstring activeDevice;
    std::wstring preferredDevice;
};

std::wstring JsonEscapeWide(std::wstring_view value);
std::wstring SerializeWebViewCoreSettingsProperties(const WebViewCoreSettingsState& state);
std::wstring SerializeWebViewCoreSettingsState(const WebViewCoreSettingsState& state);
std::wstring NextNoSignalMode(std::wstring_view currentMode);
std::wstring SerializeGameResult(const Games::Result& result);
std::wstring SerializeArtworkMetadataProperties(const Games::ArtworkMetadata& artwork);
std::wstring SerializeArtworkEditProperties(const Games::SavedGame& game);
std::wstring SerializeArtworkCandidates(const std::vector<Games::Cover>& covers);

} // namespace NitLink
