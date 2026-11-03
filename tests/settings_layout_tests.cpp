#include "app/WebViewSettings.h"
#include "app/settings_message.h"
#include "app/holybear_version.h"

#include <shlwapi.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Microsoft::WRL::Callback;

struct WebViewSettingsTestAccess {
    static ICoreWebView2* View(WebViewSettings& host) { return host.m_webview.get(); }
    static ICoreWebView2Controller* Controller(WebViewSettings& host) { return host.m_controller.get(); }
    static size_t HiddenSurfaces(const WebViewSettings& host) { return host.m_hiddenSurfaces.size(); }
};

namespace {
void Check(bool result, const char* label) {
    if (!result) throw std::runtime_error(label);
    std::cout << "PASS " << label << '\n';
}
void Hr(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("WebView API failed: " + std::to_string(hr));
}
bool PumpUntil(const std::function<bool()>& predicate, int milliseconds = 10000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (predicate()) return true;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
std::wstring Script(ICoreWebView2* view, const std::wstring& script) {
    struct Result { bool done = false; HRESULT hr = E_PENDING; std::wstring value; };
    const auto result = std::make_shared<Result>();
    Hr(view->ExecuteScript(script.c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [result](HRESULT hr, LPCWSTR json) -> HRESULT {
            result->hr = hr;
            if (json) result->value = json;
            result->done = true;
            return S_OK;
        }).Get()));
    if (!PumpUntil([&] { return result->done; })) throw std::runtime_error("script timed out");
    Hr(result->hr);
    return result->value;
}

void BrowserCommand(ICoreWebView2* view, const wchar_t* method, const std::wstring& parameters) {
    const auto done = std::make_shared<bool>(false);
    const auto result = std::make_shared<HRESULT>(E_PENDING);
    Hr(view->CallDevToolsProtocolMethod(method, parameters.c_str(),
        Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [done, result](HRESULT hr, LPCWSTR) -> HRESULT {
                *result = hr;
                *done = true;
                return S_OK;
            }).Get()));
    Check(PumpUntil([&] { return *done; }), "browser input command completes");
    Hr(*result);
}

// Dispatch through Chromium's input path so native <summary> keyboard behavior
// and focus styling are exercised, including in the hidden WebView test host.
void BrowserKey(ICoreWebView2* view, const wchar_t* key, const wchar_t* code, int virtualKey) {
    for (const auto* type : {L"keyDown", L"keyUp"}) {
        std::wstring parameters = std::wstring(L"{\"type\":\"") + type +
            L"\",\"key\":\"" + key + L"\",\"code\":\"" + code +
            L"\",\"windowsVirtualKeyCode\":" + std::to_wstring(virtualKey);
        if (std::wstring_view(type) == L"keyDown" && virtualKey == VK_RETURN)
            parameters += L",\"text\":\"\\r\"";
        if (std::wstring_view(type) == L"keyDown" && virtualKey == VK_SPACE)
            parameters += L",\"text\":\" \"";
        parameters += L"}";
        BrowserCommand(view, L"Input.dispatchKeyEvent", parameters);
    }
}

// These are test values, not observations from capture hardware. Every native
// state push carries the same settings; only panelSide and locale vary.
void PushState(WebViewSettings& host, const wchar_t* side, const wchar_t* locale = L"en-US") {
    host.PostMessage(std::wstring(LR"({"state":{"hdrEnabled":true,"colorExpansion":true,
        "colorExpansionAvailable":true,"nisEnabled":true,"vsync":false,
        "lowLatency":true,"preventSleep":true,"presentPacing":"unique",
        "audioMuted":false,"volume":0.37,"pipOpacity":0.64,"aspectRatio":"16:9",
        "noSignalMode":"image","noSignalImage":"C:\\layout-test.png",
        "noSignalImageAvailable":true,"noSignalFit":"contain","noSignalDimImage":true,
        "configWarning":"","hdrAutoDetectSupported":true,
        "negotiatedWidth":1920,"negotiatedHeight":1080,"negotiatedFps":60,
        "negotiatedFormat":"NV12","linkText":"HDMI","latencyText":"--",
        "captureDevices":["Layout test device"],"activeDevice":"Layout test device",
        "availableFormats":[{"width":1920,"height":1080,"fps":60,"format":"NV12"}],
        "captureFormatOverride":{"width":0,"height":0,"fps":0,
            "fpsNumerator":0,"fpsDenominator":1,"format":""},
        "languagePreference":"system","panelWidth":500,"panelSide":")") + side +
        L"\",\"locale\":\"" + locale + L"\"}}");
    auto* view = WebViewSettingsTestAccess::View(host);
    const std::wstring expected = side == std::wstring_view(L"Full") ? L"full"
        : side == std::wstring_view(L"Left") ? L"left" : L"right";
    Check(PumpUntil([&] {
        return Script(view, L"document.documentElement.dataset.side === '" + expected +
            L"' && document.documentElement.lang === '" + locale + L"'") == L"true";
    }), "native presentation state applied");
    PumpUntil([] { return false; }, 180);
}

void Resize(WebViewSettings& host, HWND window, int width, int height,
            WebViewSettings::Dock dock, int dockWidth = 420) {
    RECT outer{0, 0, width, height};
    Check(AdjustWindowRectEx(&outer, WS_POPUP, FALSE, 0) != FALSE, "client size calculated");
    Check(SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "test window resized");
    host.SetDock(dock, dockWidth);
    host.Show(true);
    host.Resize();
    auto* view = WebViewSettingsTestAccess::View(host);
    const int cssWidth = dock == WebViewSettings::Dock::Full ? width : dockWidth;
    const bool sized = PumpUntil([&] {
        return Script(view, L"innerWidth === " + std::to_wstring(cssWidth) +
            L" && innerHeight === " + std::to_wstring(height)) == L"true";
    });
    if (!sized) {
        RECT client{}, bounds{};
        GetClientRect(window, &client);
        Hr(WebViewSettingsTestAccess::Controller(host)->get_Bounds(&bounds));
        std::wcout << L"Client " << client.right << L'x' << client.bottom << L"; bounds "
            << bounds.left << L',' << bounds.top << L',' << bounds.right << L',' << bounds.bottom
            << L"; viewport " << Script(view, L"[innerWidth,innerHeight]") << L'\n';
    }
    Check(sized, "native bounds produce expected CSS viewport");
}

const wchar_t* SharedValues = LR"JS(JSON.stringify({
    toggles: [...document.querySelectorAll('.toggle')].map(e => [e.id, e.className]),
    values: ['aspect-val', 'pacing-val', 'slider-volume-val', 'slider-pip-opacity-val',
        'no-signal-mode-val', 'no-signal-fit-val', 'no-signal-dim-val', 'meta-source',
        'meta-resolution', 'meta-format', 'meta-link', 'meta-latency'].map(id =>
        [id, document.getElementById(id).textContent]),
    selects: [...document.querySelectorAll('select, input')].map(e => [e.id, e.value]),
    activePane: document.querySelector('.pane[data-active="true"]').id,
    advancedOpen: document.querySelector('.advanced').open,
    sourceOpen: document.getElementById('source-popover').dataset.open,
    actions: [...document.querySelectorAll('[data-action]')].map(e => e.dataset.action)
}))JS";

const wchar_t* CompactSnapshot = LR"JS(JSON.stringify(
    [...document.querySelectorAll(`.rail, .rail-brand, .rail-item, .content, .topbar,
        .metabar, .meta, main, .pane, .band, .row, .row-name, .row-right, .row-chip,
        .row-value, .toggle, .slider-wrap, .slider, .language-select, .about-copy,
        .source-popover, #config-warning`)].map(e => {
        const r = e.getBoundingClientRect(), s = getComputedStyle(e);
        return {id: e.id, class: e.className, action: e.dataset.action,
            rect: [r.x, r.y, r.width, r.height],
            style: Array.from(s).map(k => [k, s.getPropertyValue(k)])};
    })
))JS";

void WriteText(const std::filesystem::path& path, const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    Check(size > 0, "snapshot encodes as UTF-8");
    std::string bytes(size, '\0');
    Check(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        bytes.data(), size, nullptr, nullptr) == size, "snapshot encoding completes");
    std::ofstream output(path, std::ios::binary);
    output << bytes;
    Check(output.good(), "layout snapshot saved");
}

void Capture(ICoreWebView2* view, const std::filesystem::path& path) {
    wil::com_ptr<IStream> stream;
    Hr(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
        FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream));
    struct Result { bool done = false; HRESULT hr = E_PENDING; };
    const auto result = std::make_shared<Result>();
    Hr(view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.get(),
        Callback<ICoreWebView2CapturePreviewCompletedHandler>([result](HRESULT hr) -> HRESULT {
            result->hr = hr;
            result->done = true;
            return S_OK;
        }).Get()));
    Check(PumpUntil([&] { return result->done; }), "native WebView preview completes");
    Hr(result->hr);
    Hr(stream->Commit(STGC_DEFAULT));
}

void PushAboutUpdate(WebViewSettings& host, const wchar_t* side, const wchar_t* locale,
    const wchar_t* status, const wchar_t* latest, bool hasNewer, bool automatic, bool busy,
    bool unread, const wchar_t* url, long long lastCheckUtc = 1791691200,
    std::wstring_view current = L"1.2.5.3-holybear") {
    PushState(host, side, locale);
    const auto timestamp = std::wstring_view(status) == L"never" ? 0 : lastCheckUtc;
    std::wstring message = L"{\"state\":{\"holyBearUpdate\":{\"current\":\"" +
        std::wstring(current) + L"\",\"latest\":\"";
    message += latest;
    message += L"\",\"lastCheckUtc\":" + std::to_wstring(timestamp);
    message += L",\"automatic\":";
    message += automatic ? L"true" : L"false";
    message += L",\"busy\":";
    message += busy ? L"true" : L"false";
    message += L",\"status\":\"";
    message += status;
    message += L"\",\"hasNewer\":";
    message += hasNewer ? L"true" : L"false";
    message += L",\"unread\":";
    message += unread ? L"true" : L"false";
    message += L",\"url\":\"";
    message += url;
    message += L"\",\"canDownload\":";
    message += hasNewer && NitLink::HolyBear::IsReleaseUrl(url) ? L"true" : L"false";
    message += L",\"transferStatus\":\"idle\",\"packageBytes\":8388608,\"publishedAt\":\"2026-11-03T00:00:00Z\",\"notes\":\"\"}}}";
    host.PostMessage(message);
    auto* view = WebViewSettingsTestAccess::View(host);
    std::wstring ready = L"(() => { const node=document.querySelector('#holy-bear-update-status'); "
        L"let expected=window.NitLinkLocales['";
    ready += locale;
    ready += L"']['update.";
    ready += status;
    ready += L"'];";
    if (std::wstring_view(status) == L"available")
        ready += L"expected=expected.replace('{version}','" +
            std::wstring(latest).substr(1, std::wstring(latest).size() - 10) + L"')+' · 8 MB · '+new Date('2026-11-03T00:00:00Z').toLocaleDateString('" + locale + L"');";
    ready += L"const date = new Date(" + std::to_wstring(timestamp) + L" * 1000); "
        L"const expectedTime = " + std::to_wstring(timestamp) + L" > 0 && !Number.isNaN(date.getTime()) "
        L"? date.toLocaleString('" + locale + L"') : window.NitLinkLocales['" + locale + L"']['update.never']; ";
    ready += L"return document.documentElement.lang==='";
    ready += locale;
    ready += L"' && node.textContent===expected && node.hidden===";
    ready += std::wstring_view(status) == L"never" ? L"true" : L"false";
    ready += L" && document.querySelector('#toggle-holy-bear-auto-update').getAttribute('aria-checked')==='";
    ready += automatic ? L"true" : L"false";
    ready += L"' && document.querySelector('#holy-bear-last-check').textContent === expectedTime; })()";
    Check(PumpUntil([&] {
        return Script(view, ready) == L"true";
    }), "mock update state reaches About UI");
}

void SelectAbout(ICoreWebView2* view, bool allowLegacyCapture = false) {
    Script(view, L"document.querySelector('.rail-item[data-tab=\"pane-about\"]').click()");
    Check(PumpUntil([&] {
        return Script(view, allowLegacyCapture
            ? L"document.querySelector('#pane-about').dataset.active === 'true'"
            : L"document.documentElement.dataset.activePane === 'pane-about' && "
              L"document.querySelector('#pane-about').dataset.active === 'true'") == L"true";
    }), "About navigation updates the active pane");
}

void CaptureLayouts(WebViewSettings& host, HWND window, const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    auto* view = WebViewSettingsTestAccess::View(host);
    for (const auto size : {std::pair{1920, 1080}, std::pair{2560, 1440}}) {
        const auto name = std::to_wstring(size.first) + L"x" + std::to_wstring(size.second);
        for (const auto side : {L"Full", L"Left", L"Right"}) {
            const auto dock = side == std::wstring_view(L"Full") ? WebViewSettings::Dock::Full
                : side == std::wstring_view(L"Left") ? WebViewSettings::Dock::Left : WebViewSettings::Dock::Right;
            Resize(host, window, size.first, size.second, dock);
            PushState(host, side);
            Script(view, L"document.querySelector('.rail-item[data-tab=\"pane-video\"]').click()");
            PumpUntil([] { return false; }, 180);
            Capture(view, folder / (name + L"-" + side + L"-test-host.png"));
            if (dock == WebViewSettings::Dock::Full) {
                WriteText(folder / (name + L"-Full-geometry.json"), Script(view, LR"JS(JSON.stringify(
                    [...document.querySelectorAll('.content, .topbar, .topbar h1, .topbar-actions, .metabar, main')]
                        .map(e => ({name: e.className || e.tagName, rect: e.getBoundingClientRect().toJSON(),
                            rows: getComputedStyle(e).gridTemplateRows, area: getComputedStyle(e).gridArea}))
                ))JS"));
            }
            if (dock != WebViewSettings::Dock::Full) {
                for (const auto pane : {L"pane-video", L"pane-audio", L"pane-shortcuts", L"pane-about"}) {
                    Script(view, L"document.querySelector('.rail-item[data-tab=\"" + std::wstring(pane) + L"\"]').click()");
                    PumpUntil([] { return false; }, 180);
                    WriteText(folder / (name + L"-" + side + L"-" + pane + L".json"), Script(view, CompactSnapshot));
                }
            }
        }
    }
    auto captureAbout = [&](const wchar_t* name, const wchar_t* side, const wchar_t* locale,
                            const wchar_t* status, const wchar_t* latest, bool newer,
                            const wchar_t* url, int width, int height, WebViewSettings::Dock dock) {
        Resize(host, window, width, height, dock);
        PushAboutUpdate(host, side, locale, status, latest, newer, false, false, newer, url);
        SelectAbout(view, true);
        Script(view, L"document.querySelectorAll('.about-details').forEach(details => details.open = true)");
        PumpUntil([] { return false; }, 120);
        Capture(view, folder / name);
    };
    captureAbout(L"about-docked-zh-TW-current.png", L"Right", L"zh-TW", L"current",
        L"v1.2.5.3-holybear", false, L"", 1920, 1080, WebViewSettings::Dock::Right);
    Script(view, L"document.querySelector('#about-technical').open = false");
    PumpUntil([] { return false; }, 120);
    Capture(view, folder / L"about-docked-zh-TW-contributors-only.png");
    captureAbout(L"about-docked-en-US-available.png", L"Right", L"en-US", L"available",
        L"v1.2.5.3-holybear", true,
        L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5.3-holybear",
        1920, 1080, WebViewSettings::Dock::Right);
    captureAbout(L"about-docked-zh-TW-api-unavailable.png", L"Right", L"zh-TW", L"apiUnavailable",
        L"v1.2.5.3-holybear", false, L"", 1920, 1080, WebViewSettings::Dock::Right);
    captureAbout(L"about-full-1920x1080-zh-TW.png", L"Full", L"zh-TW", L"current",
        L"v1.2.5.3-holybear", false, L"", 1920, 1080, WebViewSettings::Dock::Full);
    captureAbout(L"about-full-2560x1440-zh-TW.png", L"Full", L"zh-TW", L"current",
        L"v1.2.5.3-holybear", false, L"", 2560, 1440, WebViewSettings::Dock::Full);
    // Render the production source controls through the native host. The saved
    // one-character name is a fixture; no capture hardware or user config is used.
    for (const auto* locale : {L"en-US", L"zh-TW", L"zh-CN", L"de-DE", L"ko-KR", L"ja-JP"}) {
        Resize(host, window, 1920, 1080, WebViewSettings::Dock::Full);
        PushState(host, L"Full", locale);
        std::wstring sourceState = LR"({"state":{"manualHdmiSource":"other",
            "manualHdmiSourceCustom":"熊","effectiveHdmiSource":"熊",
            "manualHdmiSourceOptions":[)";
        for (const auto& [value, label] : NitLink::kManualHdmiSources) {
            if (value != "auto") sourceState += L",";
            sourceState += L"{\"value\":\"" + std::wstring(value.begin(), value.end()) +
                L"\",\"label\":\"" + std::wstring(label) + L"\"}";
        }
        sourceState += L"]}}";
        host.PostMessage(sourceState);
        Check(PumpUntil([&] {
            return Script(view, L"document.documentElement.lang === '" + std::wstring(locale) +
                L"' && document.getElementById('hdmi-source-trigger').value === 'other' && "
                L"document.getElementById('meta-hdmi-source').textContent === '熊'") == L"true";
        }), "localized saved HDMI source reaches the native capture host");
        Script(view, LR"JS(
            document.querySelector('.rail-item[data-tab="pane-video"]').click();
            if (document.getElementById('source-popover').dataset.open !== 'true')
                document.getElementById('source-trigger').click();
        )JS");
        Check(PumpUntil([&] {
            return Script(view, LR"JS((() => {
                const p = document.getElementById('source-popover'), r = p.getBoundingClientRect();
                const strings = window.NitLinkLocales[document.documentElement.lang];
                return document.documentElement.dataset.side === 'full' && p.dataset.open === 'true' &&
                    r.width > 0 && r.left >= 0 && r.right <= innerWidth && r.top >= 0 && r.bottom <= innerHeight &&
                    document.querySelector('#hdmi-source-trigger option[value=other]').textContent === strings['hdmiSource.otherOption'] &&
                    document.querySelector('#hdmi-source-trigger option[value=auto]').textContent === strings['value.auto'];
            })())JS") == L"true";
        }), "Full source popover is localized and visible before native capture");
        PumpUntil([] { return false; }, 180);
        Capture(view, folder / (L"full-source-" + std::wstring(locale) + L".png"));
        Script(view, L"document.getElementById('hdmi-source-custom-edit').click()");
        Check(PumpUntil([&] {
            return Script(view, LR"JS((() => {
                const editor = document.getElementById('hdmi-source-custom-editor');
                const input = document.getElementById('hdmi-source-custom-input');
                const strings = window.NitLinkLocales[document.documentElement.lang];
                const r = editor.getBoundingClientRect(), p = document.getElementById('source-popover').getBoundingClientRect();
                return !editor.hidden && document.activeElement === input && input.value === '熊' &&
                    !document.getElementById('hdmi-source-custom-row').hidden && r.width > 0 &&
                    r.top >= p.top && r.bottom <= p.bottom &&
                    document.querySelector('label[for=hdmi-source-custom-input]').textContent === strings['hdmiSource.customName'] &&
                    document.getElementById('hdmi-source-custom-save').textContent === strings['hdmiSource.save'] &&
                    document.getElementById('hdmi-source-custom-cancel').textContent === strings['hdmiSource.cancel'];
            })())JS") == L"true";
        }), "Other editor shows the saved Unicode name and localized actions before native capture");
        PumpUntil([] { return false; }, 180);
        Capture(view, folder / (L"full-other-editor-" + std::wstring(locale) + L".png"));
        Script(view, L"document.getElementById('source-trigger').click()");
    }
    WriteText(folder / L"README.txt", L"Native WebView2 test-host renders of packaged NitLink settings.\n"
        L"Signal and settings are a fixed test fixture, not live capture observations.\n"
        L"Full PNGs have 1920x1080 and 2560x1440 viewports. Docked PNGs are 420px strips.\n"
        L"About PNGs exercise the localized current, available, API-error and disclosure states.\n"
        L"Full Source and expanded Other editor PNGs use the saved name 熊 in all four locales.\n"
        L"These do not replace screenshots from the NitLink application on real hardware.\n");
}

void TestLayouts(WebViewSettings& host, HWND window, std::vector<std::wstring>& messages) {
    auto* view = WebViewSettingsTestAccess::View(host);
    Resize(host, window, 1920, 1080, WebViewSettings::Dock::Right);
    PushState(host, L"Right");
    Script(view, LR"JS(
        document.querySelector('.advanced').open = true;
        document.querySelector('#source-trigger').click();
        document.querySelector('#source-trigger').click();
        window.__controls = [...document.querySelectorAll('[id], [data-action]')]
            .filter(e => !e.closest('#source-popover'));
    )JS");
    const auto values = Script(view, SharedValues);
    const auto compactRight = Script(view, CompactSnapshot);
    Resize(host, window, 1920, 1080, WebViewSettings::Dock::Left);
    PushState(host, L"Left");
    const auto compactLeft = Script(view, CompactSnapshot);
    const size_t count = messages.size();
    for (const auto size : {std::pair{1920, 1080}, std::pair{2560, 1440}}) {
        Resize(host, window, size.first, size.second, WebViewSettings::Dock::Full);
        PushState(host, L"Full");
        Check(Script(view, LR"JS((() => {
            const meta = document.querySelector('.metabar').getBoundingClientRect();
            const main = document.querySelector('main').getBoundingClientRect();
            const pane = document.querySelector('#pane-video').getBoundingClientRect();
            const groups = [...document.querySelectorAll('#pane-video > .settings-group')];
            return meta.right === main.left && meta.top === main.top && main.right === innerWidth &&
                pane.width === main.width && groups.length === 6 &&
                groups[0].getBoundingClientRect().left < groups[1].getBoundingClientRect().left &&
                getComputedStyle(document.querySelector('.content')).display === 'grid' &&
                document.querySelector('.topbar h1').getBoundingClientRect().top >= 0;
        })())JS") == L"true", "Full uses left signal column and the entire main area with two settings columns");
        const auto observed = Script(view, SharedValues);
        if (observed != values) std::wcout << L"Expected " << values << L"\nObserved " << observed << L'\n';
        Check(observed == values, "Full retains settings, actions, active pane and advanced disclosure");
        Check(Script(view, LR"JS(
            [...document.querySelectorAll(`[data-action=toggleLowLatency], [data-action=cyclePresentPacing],
                [data-action=toggleNIS], [data-action=toggleHDR]`)].every(row => {
                const group = row.querySelector('.row-right'), text = group.firstElementChild,
                    control = group.lastElementChild;
                return getComputedStyle(group).justifyContent === 'flex-end' &&
                    getComputedStyle(text).textAlign === 'right' &&
                    control.getBoundingClientRect().right === group.getBoundingClientRect().right &&
                    control.getBoundingClientRect().left - text.getBoundingClientRect().right === 12;
            }) && document.querySelector('#slider-pip-opacity').getBoundingClientRect().width >= 150
        )JS") == L"true", "Full control text stays next to its toggle or chevron with a 12px gap and sliders keep their width");
    }
    for (const auto side : {L"Left", L"Right"}) {
        Resize(host, window, 1920, 1080, side == std::wstring_view(L"Left")
            ? WebViewSettings::Dock::Left : WebViewSettings::Dock::Right);
        PushState(host, side);
        Check(Script(view, LR"JS(getComputedStyle(document.querySelector('.content')).display === 'flex' &&
            getComputedStyle(document.querySelector('.rail')).height === '56px' &&
            getComputedStyle(document.querySelector('.rail')).flexDirection === 'row' &&
            getComputedStyle(document.querySelector('.metabar')).display === 'grid' &&
            getComputedStyle(document.querySelector('.row')).paddingLeft === '16px' &&
            getComputedStyle(document.querySelector('#pane-video')).display === 'block')JS") == L"true",
            "Left and Right retain the compact navigation, metabar, row spacing and pane layout");
        Check(Script(view, CompactSnapshot) == (side == std::wstring_view(L"Left") ? compactLeft : compactRight),
            "Full round trip leaves every compact control's geometry and computed styles unchanged");
        Check(Script(view, SharedValues) == values, "docked modes retain the same settings and actions");
    }
    Check(messages.size() == count, "presentation changes emit no settings actions");
    Check(Script(view, L"__controls.every(e => e.isConnected) && new Set([...document.querySelectorAll('[id]')].map(e => e.id)).size === document.querySelectorAll('[id]').length") == L"true",
        "presentation changes retain the original controls without duplicate IDs");

    Resize(host, window, 1920, 1080, WebViewSettings::Dock::Full);
    PushState(host, L"Full");
    Script(view, L"document.querySelector('#source-trigger').click()");
    std::wcout << L"Source popover integration geometry: " << Script(view, LR"JS((() => {
        const p = document.querySelector('#source-popover');
        p.querySelector('.source-format-details').open = true;
        const r = p.getBoundingClientRect();
        return JSON.stringify({open:p.dataset.open,viewport:[innerWidth,innerHeight],
            bounds:[r.left,r.top,r.right,r.bottom],selects:[...p.querySelectorAll('select')]
                .map(e=>({id:e.id,label:e.previousElementSibling?.textContent,
                    capture:e.parentElement.parentElement===p}))});
    })())JS") << L'\n';
    Check(Script(view, LR"JS((() => {
        const p = document.querySelector('#source-popover');
        p.querySelector('.source-format-details').open = true;
        const r = p.getBoundingClientRect();
        const capture = [...p.querySelectorAll('.source-format-details > .source-popover-select-row > select')];
        const identity = p.querySelector('#hdmi-source-picker #hdmi-source-trigger');
        const labels = ['source.resolution', 'source.framerate', 'source.format'];
        const strings = window.NitLinkLocales[document.documentElement.lang];
        return p.dataset.open === 'true' && r.width > 0 && r.left >= 0 && r.right <= innerWidth &&
            r.top >= 0 && r.bottom <= innerHeight && p.querySelectorAll('select').length === 4 &&
            identity?.tagName === 'SELECT' && identity.getBoundingClientRect().width > 0 &&
            capture.length === 3 && capture.every((e, i) =>
                e.previousElementSibling.textContent === strings[labels[i]] &&
                e.getBoundingClientRect().width > 0);
    })())JS") == L"true", "source picker and format controls remain reachable in the signal sidebar");
    Script(view, L"document.querySelector('#source-trigger').click()");

    for (const int width : {640, 900, 1200}) {
        for (const auto side : {L"Left", L"Right"}) {
            Resize(host, window, 1920, 1080, side == std::wstring_view(L"Left")
                ? WebViewSettings::Dock::Left : WebViewSettings::Dock::Right, width);
            PushState(host, side);
            Check(Script(view, L"getComputedStyle(document.querySelector('.content')).display === 'flex' && getComputedStyle(document.querySelector('.settings-group')).display === 'contents'") == L"true",
                "wider docked panels never opt into the Full presentation");
        }
    }

    for (const auto size : {std::pair{640, 480}, std::pair{1024, 576}, std::pair{1280, 720}}) {
        Resize(host, window, size.first, size.second, WebViewSettings::Dock::Full);
        PushState(host, L"Full", L"zh-TW");
        Check(Script(view, LR"JS((() => {
            const main = document.querySelector('main'), r = main.getBoundingClientRect();
            return r.width > 0 && r.height > 0 && main.scrollWidth <= main.clientWidth &&
                document.documentElement.scrollWidth === innerWidth;
        })())JS") == L"true", "smaller Full windows keep localized settings scrollable without horizontal overflow");
    }

    for (const auto side : {L"Full", L"Left", L"Right"}) {
        const auto dock = side == std::wstring_view(L"Full") ? WebViewSettings::Dock::Full
            : side == std::wstring_view(L"Left") ? WebViewSettings::Dock::Left : WebViewSettings::Dock::Right;
        Resize(host, window, 1920, 1080, dock);
        PushState(host, side);
        const size_t before = messages.size();
        Script(view, L"document.querySelector('[data-action=toggleHDR]').click()");
        Check(PumpUntil([&] { return messages.size() == before + 1; }), "shared toggle posts exactly one action");
        const auto toggle = NitLink::ParseSettingsMessage(messages.back());
        Check(toggle && toggle->action == L"toggleHDR", "identical HDR action crosses the native bridge in every presentation");
        Script(view, L"document.querySelector('#slider-pip-opacity').value = '64'; document.querySelector('#slider-pip-opacity').dispatchEvent(new Event('input'))");
        Check(PumpUntil([&] { return messages.size() == before + 2; }), "shared slider posts exactly one action");
        const auto slider = NitLink::ParseSettingsMessage(messages.back());
        Check(slider && slider->action == L"setPiPOpacity" && slider->number == 0.64,
            "identical slider value crosses the native bridge in every presentation");
        Script(view, L"document.querySelector('.rail-item[data-tab=\"pane-audio\"]').click()");
        PushState(host, side, L"zh-TW");
        Check(Script(view, L"document.querySelector('#pane-audio').dataset.active === 'true' && document.querySelector('#slider-volume-val').textContent === '37'") == L"true",
            "navigation and localized state use the same hidden and visible controls");
    }
}

void TestAudioDiagnostics(WebViewSettings& host, std::vector<std::wstring>& messages) {
    auto* view = WebViewSettingsTestAccess::View(host);
    Check(Script(view, L"document.querySelector('#audio-diagnostics').hidden && !document.querySelector('#audio-debug').checked") == L"true",
        "audio diagnostics remain hidden by default");
    Check(Script(view, LR"JS((() => {
        const keys = ['audio.priming','audio.rebuffering','audio.captureStalled','audio.recovering',
            'audio.confidence','audio.localReprimes','audio.startupSilence','audio.rebufferFrames',
            'audio.resetDiscardFrames','audio.srcBypass','audio.trace','audio.exportTrace',
            'audio.traceHelp','audio.countersHelp','audio.clockLearning','audio.clockHolding','audio.clockUnavailable'];
        return ['en-US','zh-TW','zh-CN','de-DE','ko-KR','ja-JP'].every(locale =>
            keys.every(key => typeof window.NitLinkLocales[locale][key] === 'string' &&
                window.NitLinkLocales[locale][key].length > 0));
    })())JS") == L"true", "all six locales define the audio diagnostics and export controls");
    host.PostMessage(LR"({"state":{"locale":"zh-CN","audioPlaybackState":"captureStalled",
        "audioStreaming":false,"audioTraceAvailable":true,"audioFillMs":3,"audioClockConfidence":87,
        "audioStartupSilence":111,"audioRebufferFrames":222,"audioResetDiscardFrames":333,
        "audioLocalReprimes":4,"audioUnderruns":555,"audioOverruns":666,"audioResyncs":7,
        "audioSrcBypassed":true,"audioDrift":false}})");
    Check(PumpUntil([&] {
        return Script(view, LR"JS(document.querySelector('#audio-status').textContent === window.NitLinkLocales['zh-CN']['audio.captureStalled'] &&
            document.querySelector('#audio-health').textContent.includes('87%') &&
            !document.querySelector('#audio-export-trace').disabled)JS") == L"true";
    }), "native state distinguishes capture stalls and enables trace export");
    Check(Script(view, LR"JS((() => {
        const text = document.querySelector('#audio-health').textContent;
        return document.querySelector('#audio-diagnostics').hidden &&
            ['111','222','333','555','666',window.NitLinkLocales['zh-CN']['audio.srcBypass']].every(value => text.includes(value));
    })())JS") == L"true", "hidden diagnostics retain distinct counters and correctly identify SRC bypass");
    const size_t before = messages.size();
    Script(view, L"document.querySelector('#audio-debug').click()");
    Check(Script(view, L"!document.querySelector('#audio-diagnostics').hidden && document.querySelector('#audio-debug').getAttribute('aria-expanded') === 'true'") == L"true",
        "diagnostics visibility is an accessible local presentation toggle");
    Check(messages.size()==before,"diagnostics toggle never changes audio processing through the native bridge");
    Script(view, L"document.querySelector('#audio-export-trace').click()");
    Check(PumpUntil([&] { return messages.size()==before+1; }),"trace export posts exactly one native action");
    const auto exported = NitLink::ParseSettingsMessage(messages.back());
    Check(exported && exported->action==L"exportAudioTrace" && exported->text.empty(),
        "trace export delegates destination selection to native code");
    Check(Script(view,L"Object.keys(" + messages.back() + L").length") == L"1",
        "trace export message contains no value or path");
    Script(view, L"document.querySelector('#audio-debug').click()");
    Check(Script(view, L"document.querySelector('#audio-diagnostics').hidden") == L"true",
        "diagnostics can be hidden after export");
    for (unsigned state : {0u,1u,2u,3u}) {
        host.PostMessage(L"{\"state\":{\"audioClockState\":" + std::to_wstring(state) +
            LR"(,"audioClockValid":false,"audioEstimatedPpm":123,"audioClockConfidence":61,"audioFillMs":3}})");
        const auto script = L"(() => { const state = " + std::to_wstring(state) + LR"JS(;
            const words = window.NitLinkLocales['zh-CN'];
            const states = [words['audio.clockLearning'], '123 ppm',
                '123 ppm (' + words['audio.clockHolding'] + ')', words['audio.clockUnavailable']];
            const line = document.querySelector('#audio-health').textContent.split('\n')
                .find(value => value.startsWith(words['audio.clockEstimate'] + ' '));
            return line === words['audio.clockEstimate'] + ' ' + states[state] + ' · ' + words['audio.confidence'] + ' 61%';
        })())JS";
        Check(PumpUntil([&] { return Script(view,script)==L"true"; }),
            "clock display distinguishes learning, fresh, holding and unavailable while keeping confidence");
    }
}


void TestAudioRefresh(WebViewSettings& host, HWND window, std::vector<std::wstring>& messages) {
    auto* view = WebViewSettingsTestAccess::View(host);
    const auto firstMessage = messages.size();
    // Synthetic diagnostics exercise the native WebView bridge, without opening
    // a capture device or sound endpoint. Unchanged counters are valid fresh data.
    const auto pushAudio = [&](uint32_t request, double fill) {
        std::wstringstream state;
        state << L"{\"state\":{\"audioFillMs\":" << fill <<
            LR"(,"audioQueueMs":0,"audioRenderMs":10,"audioEffectiveMs":3,
                "audioAverageMs":12,"audioMinMs":7,"audioMaxMs":17,
                "audioPeriodUs":3000,"audioPpm":-21,"audioEstimatedPpm":-21,
                "audioClockValid":true,"audioUnderrunEvents":0,"audioReloads":0,
                "audioUnderruns":0,"audioOverruns":7,"audioResyncs":2,
                "audioAdaptive":true,"audioStreaming":true,"audioDrift":true,
                "audioExclusiveActive":true,"audioEndpointInfo":"test snapshot")";
        if (request) state << L",\"audioRefreshRequest\":" << request;
        state << L"}}";
        Check(host.PostMessage(state.str()), "synthetic audio snapshot accepted by native bridge");
        Check(PumpUntil([&] {
            return Script(view, L"document.getElementById('audio-health').textContent.includes('" +
                std::to_wstring(fill).substr(0, 5) + L" ms')") == L"true";
        }), "received audio data renders before refresh feedback");
    };
    const auto requestFromClick = [&](size_t previous) {
        Check(PumpUntil([&] { return messages.size() > previous; }), "refresh reaches native handler");
        const auto request = NitLink::ParseSettingsMessage(messages.back());
        Check(request && request->action == L"refreshAudioDiagnostics" && request->number > 0,
            "refresh uses a validated request ID instead of page readiness");
        Check(Script(view, LR"JS((() => {
            const b = document.getElementById('audio-refresh');
            return b.disabled && b.getAttribute('aria-busy') === 'true' &&
                b.textContent === window.NitLinkLocales[document.documentElement.lang]['audio.refreshing'];
        })())JS") == L"true", "click shows localized busy feedback without claiming success");
        return static_cast<uint32_t>(request->number);
    };
    const auto busy = [&] {
        return Script(view, L"document.getElementById('audio-refresh').disabled && "
            L"document.getElementById('audio-refresh').dataset.i18n === 'audio.refreshing'") == L"true";
    };
    for (const auto size : {std::pair{1920, 1080}, std::pair{2560, 1440}}) {
        for (const auto* side : {L"Full", L"Right", L"Left"}) {
            const auto dock = std::wstring_view(side) == L"Full" ? WebViewSettings::Dock::Full
                : std::wstring_view(side) == L"Left" ? WebViewSettings::Dock::Left : WebViewSettings::Dock::Right;
            Resize(host, window, size.first, size.second, dock);
            for (const auto* locale : {L"en-US", L"zh-TW", L"zh-CN", L"de-DE", L"ko-KR", L"ja-JP"}) {
                PushState(host, side, locale);
                Script(view, LR"JS((() => {
                    document.querySelector('.rail-item[data-tab="pane-audio"]').click();
                    if (!document.getElementById('audio-debug').checked) document.getElementById('audio-debug').click();
                    const b = document.getElementById('audio-refresh');
                    b.blur(); b.scrollIntoView({block:'center'});
                })())JS");
                pushAudio(0, 11.11);
                Check(Script(view, LR"JS((() => {
                    const health = document.getElementById('audio-health');
                    const label = document.getElementById('audio-refresh').closest('.row').querySelector('.row-name');
                    const inset = health.getBoundingClientRect().left + parseFloat(getComputedStyle(health).paddingLeft);
                    const reference = getComputedStyle(document.getElementById('shortcut-reset'));
                    return Math.abs(inset - label.getBoundingClientRect().left) < 1 &&
                        health.querySelectorAll('.audio-health-group').length === 5 &&
                        ['audio-refresh','audio-export-trace'].every(id => {
                            const b = document.getElementById(id), style = getComputedStyle(b);
                            const range = document.createRange(); range.selectNodeContents(b);
                            const natural = range.getBoundingClientRect().width + parseFloat(style.paddingLeft) +
                                parseFloat(style.paddingRight) + parseFloat(style.borderLeftWidth) + parseFloat(style.borderRightWidth);
                            return Math.abs(b.getBoundingClientRect().width - natural) < 2 &&
                                style.borderRadius === reference.borderRadius && style.fontSize === reference.fontSize;
                        }) && health.scrollWidth <= health.clientWidth;
                })())JS") == L"true", "diagnostics align with row labels and both buttons use the common content-sized style");
                if (std::wstring_view(locale) == L"zh-TW") {
                    for (const auto effect : {L"classic", L"blur"}) {
                        host.PostMessage(L"{\"state\":{\"panelEffect\":\"" + std::wstring(effect) + L"\"}}");
                        Check(PumpUntil([&] { return Script(view, L"document.documentElement.dataset.panelEffect === '" +
                            std::wstring(effect) + L"'") == L"true"; }), "audio panel effect changes through native state");
                        Check(Script(view, LR"JS((() => {
                            const row = document.getElementById('audio-trace-row');
                            const after = getComputedStyle(row,'::after');
                            return getComputedStyle(row).borderBottomWidth === '0px' &&
                                (after.display === 'none' || after.content === 'none' || after.content === 'normal');
                        })())JS") == L"true",
                            "the trace row has no bottom divider in either panel style");
                    }
                }
                BrowserCommand(view, L"Input.dispatchMouseEvent", L"{\"type\":\"mouseMoved\",\"x\":0,\"y\":0}");
                PumpUntil([] { return false; }, 150);
                Script(view, L"window.__refreshNormal = getComputedStyle(document.getElementById('audio-refresh')).backgroundColor");
                const auto pointer = [&](const wchar_t* type) {
                    BrowserCommand(view, L"Input.dispatchMouseEvent", Script(view,
                        L"(() => {const r=document.getElementById('audio-refresh').getBoundingClientRect();"
                        L"return {type:'" + std::wstring(type) + L"',x:r.left+r.width/2,y:r.top+r.height/2,"
                        L"button:'left',clickCount:1};})()"));
                };
                pointer(L"mouseMoved");
                Check(PumpUntil([&] {
                    return Script(view, L"getComputedStyle(document.getElementById('audio-refresh')).backgroundColor !== window.__refreshNormal") == L"true";
                }), "real Chromium pointer hover highlights refresh in every layout and locale");
                Script(view, L"window.__refreshHover = getComputedStyle(document.getElementById('audio-refresh')).backgroundColor");
                pointer(L"mousePressed");
                Check(PumpUntil([&] {
                    return Script(view, L"document.getElementById('audio-refresh').matches(':active') && "
                        L"getComputedStyle(document.getElementById('audio-refresh')).backgroundColor !== window.__refreshHover") == L"true";
                }), "pressed refresh has visible feedback before the click is sent");
                const auto count = messages.size();
                pointer(L"mouseReleased");
                const auto request = requestFromClick(count);
                Script(view, L"document.getElementById('audio-refresh').click()");
                Check(messages.size() == count + 1, "busy refresh prevents duplicate native requests");
                pushAudio(0, 12.12);
                Check(busy(), "ordinary audio broadcast cannot acknowledge refresh");
                pushAudio(request + 1, 13.13);
                Check(busy(), "unmatched audio response cannot acknowledge refresh");
                host.PostMessage(L"{\"state\":{\"audioRefreshRequest\":" + std::to_wstring(request) + L"}}");
                Script(view, L"true");
                Check(busy(), "matching ID without audio data cannot claim success");
                pushAudio(request, 12.34);
                Check(PumpUntil([&] { return Script(view, LR"JS((() => {
                    const b=document.getElementById('audio-refresh');
                    return !b.disabled && b.getAttribute('aria-busy') === 'false' &&
                        b.textContent === window.NitLinkLocales[document.documentElement.lang]['audio.refreshed'] &&
                        document.getElementById('audio-health').textContent.includes('12.34 ms');
                })())JS") == L"true"; }), "only matching rendered audio data shows localized Updated feedback");
                Check(Script(view, L"document.querySelector('#pane-audio').scrollWidth <= document.querySelector('#pane-audio').clientWidth && "
                    L"document.documentElement.scrollWidth <= innerWidth") == L"true", "audio feedback does not overflow Full or Docked at 1080p and 1440p");
                // A second fresh response may legitimately contain identical values.
                Script(view, L"document.getElementById('audio-refresh').click()");
                const auto unchanged = requestFromClick(count + 1);
                pushAudio(unchanged, 12.34);
                Check(PumpUntil([&] { return !busy(); }), "fresh unchanged counters still complete refresh");
            }
        }
    }
    Check(PumpUntil([&] { return Script(view, L"document.getElementById('audio-refresh').dataset.i18n === 'audio.refresh'") == L"true"; }, 3000),
        "success feedback returns to the localized idle label");
    Script(view, L"document.getElementById('audio-debug').focus()");
    BrowserKey(view, L"Tab", L"Tab", VK_TAB);
    Check(Script(view, L"document.activeElement.matches('[data-audio-help=\"audio.countersHelp\"]')") == L"true",
        "keyboard navigation reaches the new counter explanation before refresh");
    BrowserKey(view, L"Tab", L"Tab", VK_TAB);
    Check(Script(view, L"document.activeElement === document.getElementById('audio-refresh') && "
        L"getComputedStyle(document.getElementById('audio-refresh')).outlineWidth === '2px'") == L"true", "keyboard navigation retains refresh focus styling");
    auto count = messages.size();
    BrowserKey(view, L" ", L"Space", VK_SPACE);
    const auto timedOut = requestFromClick(count);
    Check(PumpUntil([&] { return Script(view, L"document.getElementById('audio-refresh').dataset.i18n === 'audio.refreshFailed' && "
        L"!document.getElementById('audio-refresh').disabled") == L"true"; }, 6500), "missing response restores button without claiming an audio-engine failure");
    pushAudio(timedOut, 14.14);
    Check(Script(view, L"document.getElementById('audio-refresh').dataset.i18n !== 'audio.refreshed'") == L"true", "late timed-out response never shows Updated");
    count = messages.size();
    Script(view, L"document.getElementById('audio-refresh').click()");
    const auto retry = requestFromClick(count);
    pushAudio(timedOut, 15.15);
    Check(busy(), "old response cannot acknowledge retry");
    pushAudio(retry, 12.34);
    Check(!busy(), "retry completes with its own fresh response");
    for (const auto* locale : {L"en-US", L"zh-TW", L"zh-CN", L"de-DE", L"ko-KR", L"ja-JP"}) {
        PushState(host, L"Left", locale);
        Check(Script(view, LR"JS((() => {
            const b=document.getElementById('audio-refresh'), original=chrome.webview.postMessage;
            try {
                chrome.webview.postMessage=undefined;
                b.click();
                return !b.disabled && b.getAttribute('aria-busy') === 'false' &&
                    b.textContent === window.NitLinkLocales[document.documentElement.lang]['audio.refreshFailed'];
            } finally { chrome.webview.postMessage=original; }
        })())JS") == L"true", "unavailable bridge feedback is localized and does not claim success");
    }
    for (size_t i = firstMessage; i < messages.size(); ++i) {
        const auto message = NitLink::ParseSettingsMessage(messages[i]);
        Check(message && message->action == L"refreshAudioDiagnostics", "refresh changes no audio or capture setting");
    }
}

void TestUpdateFlow(WebViewSettings& host, HWND window, std::vector<std::wstring>& messages,
                    const std::filesystem::path& captures = {}) {
    Resize(host, window, 1120, 840, WebViewSettings::Dock::Full);
    PushState(host, L"Full", L"zh-TW");
    auto* view = WebViewSettingsTestAccess::View(host);
    const std::wstring prefix = LR"({"state":{"holyBearUpdate":{"current":"1.2.5.3-holybear","latest":"v1.2.5.4-holybear","lastCheckUtc":1793692800,"status":"available","hasNewer":true,"unread":true,"busy":false,"canDownload":true,"packageBytes":8388608,"receivedBytes":4194304,"publishedAt":"2026-11-03T08:00:00Z","notes":"## 更新內容\n\n- **程式內更新**：查看日誌、下載與安裝都在 NitLink 完成。\n- 更新時保留設定、帳號連動及遊戲圖片。\n\n### 操作方式\n\n1. 點選「下載更新」。\n2. 等待下載與驗證完成。\n3. 點選「安裝更新」，完成後自動重新開啟。\n\n```text\nNitLink HolyBear Edition\n```\n\n[版本說明](https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5.4-holybear)\n\n<script>window.__updateInjected=true</script><img src='https://blocked.invalid/notes' onerror='window.__updateInjected=true'><button data-action='installHolyBearUpdate'></button><a href='javascript:alert(1)'></a>","transferStatus":")";
    const auto push = [&](const wchar_t* phase, bool automatic = true) {
        host.PostMessage(prefix + phase + L"\",\"automatic\":" + (automatic ? L"true" : L"false") + L"}}}");
        Check(PumpUntil([&] { return Script(view, L"document.querySelector('.about-update').dataset.transfer === '" +
            std::wstring(phase) + L"'") == L"true"; }), "update transfer state reaches the real native WebView");
    };
    push(L"idle");
    Script(view, L"document.querySelector('.rail-item[data-tab=\"pane-video\"]').click()");
    host.PostMessage(LR"({"state":{"showHolyBearUpdate":true}})");
    Check(PumpUntil([&] { return Script(view, L"document.documentElement.dataset.activePane === 'pane-about'") == L"true"; }),
        "native update notification opens About without visiting a web page");
    Check(Script(view, LR"JS((() => {
        const notes=document.getElementById('holy-bear-release-notes');
        return notes.querySelector('h2').textContent === '更新內容' && notes.querySelectorAll('li').length === 5 &&
            notes.querySelector('strong').textContent === '程式內更新' && !!notes.querySelector('pre code') &&
            !!notes.querySelector('a[href="https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5.4-holybear"]') &&
            !notes.querySelector('script,img,button,input,iframe,style,[data-action],[onerror]') &&
            ![...notes.querySelectorAll('a')].some(a=>a.getAttribute('href')?.startsWith('javascript:')) &&
            !window.__updateInjected;
    })())JS") == L"true", "Markdown headings, lists, emphasis, code and links render while active HTML is removed");
    for (const auto dock : {WebViewSettings::Dock::Full, WebViewSettings::Dock::Left, WebViewSettings::Dock::Right}) {
        const auto side = dock == WebViewSettings::Dock::Full ? L"Full" : dock == WebViewSettings::Dock::Left ? L"Left" : L"Right";
        Resize(host, window, 1920, 1080, dock);
        PushState(host, side, L"zh-TW");
        Script(view, L"document.querySelector('main').scrollTop = 0");
        Check(Script(view, LR"JS((() => {
            const main = document.querySelector('main').getBoundingClientRect();
            const card = document.querySelector('.about-update').getBoundingClientRect();
            const full = document.documentElement.dataset.side === 'full';
            return Math.abs(card.left - main.left - (full ? 22 : 16)) < 1 &&
                Math.abs(card.top - main.top - (full ? 22 : 10)) < 1;
        })())JS") == L"true", "update card uses one content inset in Full and both docked layouts");
    }
    Resize(host, window, 1120, 840, WebViewSettings::Dock::Full);
    PushState(host, L"Full", L"zh-TW");
    Script(view, L"document.querySelector('main').scrollTop = 0");
    if (!captures.empty()) {
        std::filesystem::create_directories(captures);
        Capture(view, captures / L"update-available-native-fixture.png");
    }
    auto before = messages.size();
    Script(view, L"document.getElementById('holy-bear-check-update').click()");
    Check(PumpUntil([&] { return messages.size() == before + 1; }) &&
        NitLink::ParseSettingsMessage(messages.back())->action == L"downloadHolyBearUpdate",
        "Download sends only the native update action, never a URL or a filesystem path");
    Script(view, L"window.__updateNotesFirst = document.getElementById('holy-bear-release-notes').firstElementChild");
    push(L"downloading");
    Check(Script(view, L"document.getElementById('holy-bear-check-update').disabled && "
        L"document.getElementById('holy-bear-download-progress').value === 50 && "
        L"!document.getElementById('holy-bear-cancel-download').hidden && "
        L"window.__updateNotesFirst === document.getElementById('holy-bear-release-notes').firstElementChild") == L"true",
        "download progress updates without replacing the Markdown or enabling duplicate downloads");
    if (!captures.empty()) Capture(view, captures / L"update-downloading-native-fixture.png");
    before = messages.size();
    Script(view, L"document.getElementById('holy-bear-cancel-download').click()");
    Check(PumpUntil([&] { return messages.size() == before + 1; }) &&
        NitLink::ParseSettingsMessage(messages.back())->action == L"cancelHolyBearUpdateDownload", "Cancel reaches the native transfer worker");
    push(L"cancelled");
    Check(Script(view, L"!document.getElementById('holy-bear-check-update').disabled && document.getElementById('holy-bear-download-progress').hidden") == L"true",
        "cancelled download can be started again");
    push(L"ready");
    Check(Script(view, L"document.getElementById('holy-bear-check-update').textContent === NitLinkLocales['zh-TW']['update.install'] && "
        L"!document.getElementById('holy-bear-check-update').disabled && document.getElementById('holy-bear-download-progress').hidden") == L"true",
        "a verified Ready state becomes Install rather than opening a download folder");
    if (!captures.empty()) Capture(view, captures / L"update-ready-native-fixture.png");
    before = messages.size();
    Script(view, L"document.getElementById('holy-bear-check-update').click()");
    Check(PumpUntil([&] { return messages.size() == before + 1; }) &&
        NitLink::ParseSettingsMessage(messages.back())->action == L"installHolyBearUpdate", "Install reaches the native installer operation");
    push(L"ready", false);
    Check(Script(view, L"document.getElementById('holy-bear-update-badge').hidden && "
        L"document.getElementById('toggle-holy-bear-auto-update').getAttribute('aria-checked') === 'false' && "
        L"document.getElementById('holy-bear-check-update').textContent === NitLinkLocales['zh-TW']['update.install']") == L"true",
        "automatic opt-out suppresses the notification while explicit installation stays available");
}

void TestAbout(WebViewSettings& host, HWND window, std::vector<std::wstring>& messages,
               const std::filesystem::path& captures = {}) {
    auto* view = WebViewSettingsTestAccess::View(host);
    const bool requireGitHubAvatars = !captures.empty();
    Resize(host, window, 1920, 1080, WebViewSettings::Dock::Right);
    Script(view, L"document.querySelector('.rail-item[data-tab=\"pane-video\"]').click()");
    PumpUntil([] { return false; }, 120);
    PushAboutUpdate(host, L"Right", L"zh-TW", L"current", L"v1.2.5.3-holybear",
        false, false, false, false, L"");
    const auto settingsBefore = Script(view, SharedValues);
    const size_t beforeAbout = messages.size();
    SelectAbout(view);
    Check(messages.size() == beforeAbout + 1 && messages.back().find(L"markHolyBearUpdateSeen") != std::wstring::npos,
        "opening About only acknowledges the current update notification");
    Check(Script(view, LR"JS((() => {
        const pane = document.querySelector('#pane-about'), heroText = pane.querySelector('.about-hero').innerText;
        return document.documentElement.dataset.activePane === 'pane-about' &&
            getComputedStyle(document.querySelector('.metabar')).display === 'none' &&
            document.querySelectorAll('#pane-about > .about-details').length === 2 &&
            [...document.querySelectorAll('.about-details')].every(details => {
                const summary = details.querySelector('summary');
                return details.open && summary.getAttribute('aria-expanded') === 'true' &&
                    document.getElementById(summary.getAttribute('aria-controls')).parentElement === details &&
                    summary.tabIndex >= 0;
            }) &&
            document.querySelector('#holy-bear-product-title').textContent === 'NitLink 1.2.5.3 HolyBear Edition' &&
            // The injected product version is 1.2.5.3, and the upstream-base
            // subtitle now independently names 1.2.5 as well.
            (heroText.match(/1\.2\.5\.3/g) || []).length === 1 &&
            pane.querySelector('[data-i18n="about.subtitle"]').textContent ===
                '非官方社群整合版本 · 基於 NitLink 1.2.5' &&
            !pane.querySelector('#holy-bear-version, #holy-bear-latest-version, .about-detail-list') &&
            document.querySelector('#holy-bear-last-check').closest('.about-update') !== null &&
            document.querySelector('#holy-bear-update-status').textContent === window.NitLinkLocales['zh-TW']['update.current'] &&
            document.querySelector('#holy-bear-update-status').hidden === false &&
            document.querySelector('#holy-bear-check-update').textContent === window.NitLinkLocales['zh-TW']['update.check'] &&
            document.querySelector('#holy-bear-update-status').textContent.indexOf('未知') < 0 &&
            !!document.querySelector('.about-links a[href="https://tally.so/r/EkZjNr"]') &&
            !!document.querySelector('.about-links a[href="https://ko-fi.com/klosed89"]');
    })())JS") == L"true", "About has two independently accessible default-open disclosures and centralized update information");
    Check(Script(view, LR"JS((() => {
        const main = document.querySelector('main');
        return document.documentElement.scrollWidth <= innerWidth && main.scrollWidth <= main.clientWidth &&
            document.querySelector('#pane-about').scrollWidth <= document.querySelector('#pane-about').clientWidth;
    })())JS") == L"true", "420px docked About has no horizontal overflow");

    const size_t sizeBeforeCheck = messages.size();
    Script(view, L"window.__lastAboutCheck = document.querySelector('#holy-bear-last-check').textContent");
    Script(view, L"document.querySelector('#holy-bear-check-update').click()");
    Check(PumpUntil([&] { return messages.size() == sizeBeforeCheck + 1; }), "manual update click posts a native action");
    const auto autoCheck = NitLink::ParseSettingsMessage(messages.back());
    Check(autoCheck && autoCheck->action == L"checkHolyBearUpdate" &&
        Script(view, LR"JS(document.querySelector('#holy-bear-update-status').textContent ===
            window.NitLinkLocales[document.documentElement.lang]['update.checking'] &&
            document.querySelector('#holy-bear-check-update').disabled &&
            document.querySelector('#holy-bear-last-check').textContent === window.__lastAboutCheck)JS") == L"true",
        "manual button retains its action and busy state without inventing a completion time");

    PushAboutUpdate(host, L"Right", L"zh-TW", L"current", L"v1.2.5.3-holybear",
        false, false, false, false, L"", 1791691320);
    Check(Script(view, L"document.querySelector('#holy-bear-last-check').textContent !== window.__lastAboutCheck && "
        L"!document.querySelector('#holy-bear-check-update').disabled") == L"true",
        "manual completion refreshes the localized last-check time from native state");
    const size_t sizeBeforeToggle = messages.size();
    Script(view, L"document.querySelector('#toggle-holy-bear-auto-update').click()");
    Check(PumpUntil([&] { return messages.size() == sizeBeforeToggle + 1; }), "automatic check switch posts one native action");
    auto automatic = NitLink::ParseSettingsMessage(messages.back());
    Check(automatic && automatic->action == L"setHolyBearAutoUpdateCheck" && automatic->number == 1 &&
        Script(view, L"document.querySelector('#toggle-holy-bear-auto-update').getAttribute('aria-checked') === 'true'") == L"true",
        "automatic check enables with an accessible switch state");
    PushAboutUpdate(host, L"Right", L"zh-TW", L"current", L"v1.2.5.3-holybear",
        false, true, false, false, L"");
    Check(Script(view, L"document.querySelector('#toggle-holy-bear-auto-update').classList.contains('on') && "
        L"document.querySelector('#toggle-holy-bear-auto-update').getAttribute('aria-checked') === 'true'") == L"true",
        "automatic check setting is restored from native state");
    Script(view, L"window.__lastAboutCheck = document.querySelector('#holy-bear-last-check').textContent");
    PushAboutUpdate(host, L"Right", L"zh-TW", L"checking", L"v1.2.5.3-holybear",
        false, true, true, false, L"");
    PushAboutUpdate(host, L"Right", L"zh-TW", L"current", L"v1.2.5.3-holybear",
        false, true, false, false, L"", 1791691440);
    Check(Script(view, L"document.querySelector('#holy-bear-last-check').textContent !== window.__lastAboutCheck && "
        L"document.querySelector('#toggle-holy-bear-auto-update').getAttribute('aria-checked') === 'true'") == L"true",
        "automatic completion refreshes the last-check time while retaining the opt-in state");
    const size_t sizeBeforeDisable = messages.size();
    Script(view, L"document.querySelector('#toggle-holy-bear-auto-update').click()");
    Check(PumpUntil([&] { return messages.size() == sizeBeforeDisable + 1; }), "automatic check can be turned off");
    automatic = NitLink::ParseSettingsMessage(messages.back());
    Check(automatic && automatic->action == L"setHolyBearAutoUpdateCheck" && automatic->number == 0,
        "turning automatic checks off persists the false setting");
    PushAboutUpdate(host, L"Right", L"zh-TW", L"current", L"v1.2.5.3-holybear",
        false, false, false, false, L"");

    const std::array<std::pair<const wchar_t*, const wchar_t*>, 9> statuses{{
        {L"checking", L"checking"}, {L"current", L"current"}, {L"offline", L"offline"},
        {L"rateLimited", L"rateLimited"}, {L"notFound", L"notFound"},
        {L"serverUnavailable", L"serverUnavailable"}, {L"apiUnavailable", L"apiUnavailable"},
        {L"invalid", L"invalid"}, {L"never", L"never"}}};
    for (const auto* locale : {L"en-US", L"zh-TW", L"zh-CN", L"de-DE", L"ko-KR", L"ja-JP"}) {
        PushAboutUpdate(host, L"Right", locale, L"current", L"v1.2.5.3-holybear",
            false, true, false, false, L"");
        std::wstring baseCopy = L"(() => { const locale=window.NitLinkLocales['" + std::wstring(locale) +
            L"']; const subtitle=document.querySelector('[data-i18n=\"about.subtitle\"]'); "
            L"return document.documentElement.lang==='" + std::wstring(locale) +
            L"' && subtitle.textContent===locale['about.subtitle'] && subtitle.textContent.includes('1.2.5') && "
            L"!subtitle.textContent.includes('1.2.4'); })()";
        Check(Script(view, baseCopy) == L"true", "About identifies NitLink 1.2.5 as the base in all six languages");
        for (const auto& [status, key] : statuses) {
            const auto latest = std::wstring_view(status) == L"never" ? L"" : L"v1.2.5.3-holybear";
            PushAboutUpdate(host, L"Right", locale, status, latest, false,
                true, std::wstring_view(status) == L"checking", false, L"");
            std::wstring expression = L"(() => { const node=document.querySelector('#holy-bear-update-status'); return node.hidden === ";
            expression += std::wstring_view(status) == L"never" ? L"true" : L"false";
            expression += L" && node.classList.contains('is-new-version') === false && node.textContent === window.NitLinkLocales['" + std::wstring(locale) + L"']['update.";
            expression += key;
            expression += L"']; })()";
            Check(Script(view, expression) == L"true", "localized update status renders without a duplicate unknown row or false new-version alert");
        }
    }
    for (const auto timestamp : {0LL, -1LL, 9223372036854775807LL}) {
        PushAboutUpdate(host, L"Right", L"zh-TW", L"current", L"v1.2.5.3-holybear",
            false, false, false, false, L"", timestamp);
        Check(Script(view, L"document.querySelector('#holy-bear-last-check').textContent === "
            L"window.NitLinkLocales['zh-TW']['update.never']") == L"true",
            "missing, negative and out-of-range times never display an epoch or invalid date");
    }

    const std::wstring validUrl = L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5.3-holybear";
    PushAboutUpdate(host, L"Right", L"zh-TW", L"available", L"v1.2.5.3-holybear",
        true, true, false, true, validUrl.c_str());
    Check(Script(view, LR"JS((() => {
        const button = document.querySelector('#holy-bear-check-update'), status = document.querySelector('#holy-bear-update-status');
        return status.textContent.startsWith(window.NitLinkLocales['zh-TW']['update.available'].replace('{version}', '1.2.5.3')) &&
            status.classList.contains('is-new-version') &&
            button.textContent === window.NitLinkLocales['zh-TW']['update.download'] && !button.disabled &&
            document.querySelector('#holy-bear-update-badge').hidden === false;
    })())JS") == L"true", "newer release shows a localized native download action and unread state");
    for (const auto& badUrl : {L"https://github.com/HolyBearTW/nitlink/releases/tag/v1.2.5.3-holybear",
            L"https://github.com/nitlink-dev/nitlink/releases/tag/v1.2.5.3-holybear",
            L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5.3-holybear?next=evil"}) {
        PushAboutUpdate(host, L"Right", L"zh-TW", L"available", L"v1.2.5.3-holybear",
            true, true, false, true, badUrl);
        Check(Script(view, L"document.querySelector('#holy-bear-check-update').textContent === window.NitLinkLocales['zh-TW']['update.check']") == L"true",
            "unverifiable release state does not offer a download");
    }
    PushAboutUpdate(host, L"Right", L"zh-TW", L"available", L"v1.2.5.3-holybear",
        false, true, false, false, validUrl.c_str());
    Check(Script(view, L"document.querySelector('#holy-bear-check-update').textContent === window.NitLinkLocales['zh-TW']['update.check']") == L"true",
        "native download requires a newer-version result");

    PushAboutUpdate(host, L"Right", L"zh-TW", L"available", L"v1.2.5.3-holybear",
        true, true, false, true, validUrl.c_str());
    const size_t beforeDisclosure = messages.size();
    Script(view, L"document.querySelector('#about-details-summary').click()");
    Check(PumpUntil([&] { return Script(view, L"!document.querySelector('#about-technical').open && "
        L"document.querySelector('#about-details-summary').getAttribute('aria-expanded') === 'false' && "
        L"document.querySelector('#about-contributors').open && "
        L"document.querySelector('#about-contributors-summary').getAttribute('aria-expanded') === 'true'") == L"true"; }),
        "collapsing technical information leaves contributors open and synchronizes only its own ARIA state");
    Script(view, L"document.querySelector('#about-contributors-summary').click()");
    Check(PumpUntil([&] { return Script(view, L"!document.querySelector('#about-technical').open && "
        L"!document.querySelector('#about-contributors').open && "
        L"document.querySelector('#about-contributors-summary').getAttribute('aria-expanded') === 'false'") == L"true"; }),
        "contributors also collapse independently");
    BrowserCommand(view, L"Emulation.setFocusEmulationEnabled", L"{\"enabled\":true}");
    Script(view, L"document.querySelector('#about-details-summary').focus()");
    BrowserKey(view, L"Enter", L"Enter", VK_RETURN);
    std::wcout << L"Keyboard disclosure state: " << Script(view, LR"JS(JSON.stringify({
        open: document.querySelector('#about-technical').open,
        expanded: document.querySelector('#about-details-summary').getAttribute('aria-expanded'),
        active: document.activeElement.id, focused: document.hasFocus(),
        visible: document.querySelector('#about-details-summary').matches(':focus-visible'),
        outline: getComputedStyle(document.querySelector('#about-details-summary')).outlineWidth
    }))JS") << L'\n';
    Check(PumpUntil([&] { return Script(view, LR"JS((() => {
        const summary = document.querySelector('#about-details-summary');
        return document.querySelector('#about-technical').open && !document.querySelector('#about-contributors').open &&
            summary.getAttribute('aria-expanded') === 'true' && document.activeElement === summary &&
            summary.matches(':focus-visible') && getComputedStyle(summary).outlineWidth === '2px';
    })())JS") == L"true"; }), "Enter opens technical information and preserves visible keyboard focus");
    Script(view, L"document.querySelector('#about-contributors-summary').focus()");
    BrowserKey(view, L" ", L"Space", VK_SPACE);
    Check(PumpUntil([&] { return Script(view, LR"JS((() => {
        const summary = document.querySelector('#about-contributors-summary');
        return document.querySelector('#about-technical').open && document.querySelector('#about-contributors').open &&
            summary.getAttribute('aria-expanded') === 'true' && document.activeElement === summary &&
            summary.matches(':focus-visible') && getComputedStyle(summary).outlineWidth === '2px';
    })())JS") == L"true"; }), "Space opens contributors independently with the original focus styling");
    Script(view, L"document.querySelector('#about-details-summary').focus()");
    BrowserKey(view, L"Tab", L"Tab", VK_TAB);
    Check(Script(view, L"document.activeElement === document.querySelector('#about-details-body a')") == L"true",
        "Tab reaches the preserved project links within the expanded technical section");
    Check(messages.size() == beforeDisclosure, "disclosure and keyboard navigation do not post native settings actions");
    Check(Script(view, LR"JS((() => {
        const text = document.querySelector('#about-details-body').innerText;
        const contributors = document.querySelector('#about-contributors-body').innerText;
        return text.includes('D3D11') && text.includes('P010 / NV12 / BGRA') && text.includes('HDR10') && text.includes('VRR') &&
            text.includes(window.NitLinkLocales['zh-TW']['about.holyBearDisclaimer']) &&
            !text.includes('1.2.5.3-holybear') && !text.includes('v1.2.5.3-holybear') &&
            contributors.includes('CN_115 (@CN115)') && contributors.includes('聖小熊 (@HolyBearTW)') && contributors.includes('Noka123 (@Noka123)') &&
            contributors.includes('i0rph (@i0rph)') &&
            contributors.includes('nitlink-dev (@nitlink-dev)') && contributors.includes('Tavric (@tavricccc)') &&
            document.querySelector('#about-contributors-body [data-contributor]').dataset.contributor === 'nitlink-dev' &&
            !!document.querySelector('#about-contributors-body a[href="https://github.com/HolyBearTW"]') &&
            !!document.querySelector('#about-contributors-body a[href="https://github.com/CN115"]') &&
            !!document.querySelector('#about-contributors-body a[href="https://github.com/Noka123"]') &&
            !!document.querySelector('#about-contributors-body a[href="https://github.com/i0rph"]') &&
            !!document.querySelector('#about-details-body a[href="https://github.com/nitlink-dev/nitlink"]') &&
            !!document.querySelector('#about-details-body a[href="https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE"]') &&
            !!document.querySelector('#about-details-body a[href="https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md"]') &&
            !!document.querySelector('#about-details-body a[href="https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/ACKNOWLEDGMENTS.md"]');
    })())JS") == L"true", "separate sections preserve technical text, all license links and original contributor names and handles");
    Check(Script(view, LR"JS((() => {
        const main = document.querySelector('main');
        return document.documentElement.scrollWidth <= innerWidth && main.scrollWidth <= main.clientWidth &&
            document.querySelector('#pane-about').scrollWidth <= document.querySelector('#pane-about').clientWidth;
    })())JS") == L"true", "expanded 420px About remains free of horizontal overflow");

    PushAboutUpdate(host, L"Right", L"zh-CN", L"available", L"v1.2.5.3-holybear",
        true, true, false, true, validUrl.c_str());
    Check(Script(view, LR"JS(document.documentElement.lang === 'zh-CN' &&
        document.querySelector('#about-details-summary').textContent === '技术信息' &&
        document.querySelector('#about-contributors-summary').textContent === '贡献者' &&
        document.querySelector('#holy-bear-check-update').textContent === '下载更新' &&
        document.querySelector('#holy-bear-update-status').textContent.includes('发现新版本'))JS") == L"true",
        "Simplified Chinese updates the About and update controls");
    PushAboutUpdate(host, L"Right", L"en-US", L"available", L"v1.2.5.3-holybear",
        true, true, false, true, validUrl.c_str());
    Check(Script(view, LR"JS(document.documentElement.lang === 'en-US' &&
        document.querySelector('#about-details-summary').textContent === 'Technical Information' &&
        document.querySelector('#about-contributors-summary').textContent === 'Contributors' &&
        document.querySelector('#holy-bear-check-update').textContent === 'Download update' &&
        document.querySelector('#holy-bear-update-status').textContent.includes('New version found'))JS") == L"true",
        "English updates About and update controls");

    for (const auto size : {std::pair{1920, 1080}, std::pair{2560, 1440}}) {
      for (const auto* side : {L"Full", L"Right", L"Left"}) {
        const auto dock = std::wstring_view(side) == L"Full" ? WebViewSettings::Dock::Full
            : std::wstring_view(side) == L"Left" ? WebViewSettings::Dock::Left : WebViewSettings::Dock::Right;
        Resize(host, window, size.first, size.second, dock);
        for (const auto* locale : {L"en-US", L"zh-TW", L"zh-CN", L"de-DE", L"ko-KR", L"ja-JP"}) {
          PushAboutUpdate(host, side, locale, L"available", L"v1.2.4.1-holybear",
              true, true, false, false, L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.1-holybear");
          Script(view, L"document.querySelectorAll('.about-details').forEach(details => details.open = true)");
          Check(Script(view, LR"JS((() => {
              const pane = document.querySelector('#pane-about'), main = document.querySelector('main');
              const technical = document.querySelector('#about-technical').getBoundingClientRect();
              const contributors = document.querySelector('#about-contributors').getBoundingClientRect();
              const locale = window.NitLinkLocales[document.documentElement.lang];
              return [...document.querySelectorAll('.about-details')].every(details => details.open) &&
                  Math.abs(technical.left - contributors.left) < 1 && Math.abs(technical.width - contributors.width) < 1 &&
                  contributors.top >= technical.bottom && main.scrollWidth <= main.clientWidth &&
                  pane.scrollWidth <= pane.clientWidth && document.documentElement.scrollWidth <= innerWidth &&
                  document.querySelector('#about-details-summary').textContent === locale['about.technicalInformation'] &&
                  document.querySelector('#about-contributors-summary').textContent === locale['about.contributors'] &&
                  document.querySelector('[data-contributor="Noka123"]').parentElement.innerText.includes('Noka123 (@Noka123)') &&
                  !document.querySelector('[data-i18n="about.noka123Contribution"]') &&
                  document.querySelector('#holy-bear-update-status').textContent.startsWith(locale['update.available'].replace('{version}', '1.2.4.1')) &&
                  document.querySelector('#holy-bear-check-update').textContent === locale['update.download'];
          })())JS") == L"true", "Full and both Docked modes stack localized disclosures without clipping at 1080p and 1440p");
          Check(Script(view, L"getComputedStyle(document.getElementById('about-contributors')).borderBottomWidth === '0px' && "
              L"getComputedStyle(document.getElementById('about-technical')).borderBottomWidth === '1px'") == L"true",
              "only the contributors trailing divider is removed in every layout and locale");
        }
        if (dock != WebViewSettings::Dock::Full) continue;
        Check(Script(view, LR"JS((() => {
            const content = document.querySelector('.content'), main = document.querySelector('main');
            const c = content.getBoundingClientRect(), m = main.getBoundingClientRect();
            return document.documentElement.dataset.side === 'full' &&
                getComputedStyle(document.querySelector('.metabar')).display === 'none' &&
                Math.abs(m.left - c.left) < 1 && Math.abs(m.width - c.width) < 1 &&
                main.scrollWidth <= main.clientWidth && document.documentElement.scrollWidth <= innerWidth;
        })())JS") == L"true", "Full About fills the released signal column without horizontal overflow");
      }
    }

    Resize(host, window, 1920, 1080, WebViewSettings::Dock::Full);
    for (const auto* locale : {L"en-US", L"zh-TW", L"zh-CN", L"de-DE", L"ko-KR", L"ja-JP"}) {
        PushAboutUpdate(host, L"Full", locale, L"available", L"v1.2.5.3-holybear",
            true, false, false, false, L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5.3-holybear",
            1791691200, NitLink::HolyBear::Version);
        const auto numeric = NitLink::HolyBear::Version.substr(0, NitLink::HolyBear::Version.size() - 9);
        Check(Script(view, L"document.getElementById('holy-bear-product-title').textContent === 'NitLink " +
            std::wstring(numeric) + L" HolyBear Edition'") == L"true", "About displays the native four-part product version in all languages");
        Script(view, L"document.querySelector('#about-contributors').scrollIntoView({block:'center'})");
        PumpUntil([] { return false; }, 100);
        for (const auto* contributor : {L"nitlink-dev", L"HolyBearTW", L"CN115", L"n810K", L"Noka123", L"i0rph", L"tavricccc"}) {
            Script(view, L"document.querySelector('[data-contributor=\"" + std::wstring(contributor) + L"\"]').focus({preventScroll:true})");
            Check(PumpUntil([&] { return Script(view, L"(() => { const requireAvatar = " +
                std::wstring(requireGitHubAvatars ? L"true" : L"false") + LR"JS(;
                const avatarIds = {"nitlink-dev":"285324765","HolyBearTW":"50009618","CN115":"68493388",
                    "n810K":"7424933","Noka123":"82983692","i0rph":"50132390","tavricccc":"177928699"};
                const card = document.getElementById('contributor-preview');
                const text = card.querySelector('.contributor-preview-description');
                const avatar = document.getElementById('contributor-preview-avatar');
                const link = document.activeElement, bounds = card.getBoundingClientRect();
                return !card.hidden && link.dataset.contributor && link.querySelector('.about-contributor-logo') &&
                    document.getElementById('contributor-preview-handle').textContent === '@' + link.dataset.contributor &&
                    document.getElementById('contributor-preview-open').href === link.href &&
                    text.dataset.i18n !== 'about.profileDescription' &&
                    text.textContent === NitLinkLocales[document.documentElement.lang][text.dataset.i18n] &&
                    text.textContent.length > 0 && (requireAvatar
                        ? !avatar.hidden && avatar.complete && avatar.naturalWidth > 0 &&
                            avatar.src === `https://avatars.githubusercontent.com/u/${avatarIds[link.dataset.contributor]}?s=160&v=4` &&
                            avatar.referrerPolicy === 'no-referrer' && document.getElementById('contributor-preview-fallback').hidden
                        : avatar.hidden ? !document.getElementById('contributor-preview-fallback').hidden
                            : avatar.complete && avatar.naturalWidth > 0) &&
                    bounds.left >= 0 && bounds.right <= innerWidth && bounds.top >= 0 && bounds.bottom <= innerHeight;
            })())JS") == L"true"; }, 10000), "every contributor card shows its localized contribution and original profile link");
            if (!captures.empty() && std::wstring_view(locale) == L"zh-TW") {
                std::filesystem::create_directories(captures);
                Capture(view, captures / (std::wstring(contributor) + L"-contribution-native.png"));
            }
            Script(view, L"document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape'}))");
        }
    }

    Script(view, L"document.querySelector('[data-contributor=\"Noka123\"]').focus({preventScroll:true});"
        L"document.getElementById('contributor-preview-avatar').src='https://avatars.githubusercontent.com/u/0?s=160&v=4'");
    Check(PumpUntil([&] {return Script(view, L"document.getElementById('contributor-preview-avatar').hidden && "
        L"!document.getElementById('contributor-preview-fallback').hidden && "
        L"document.getElementById('contributor-preview-fallback').textContent.length > 0") == L"true";}),
        "a failed avatar request leaves a visible initials fallback");
    Script(view, L"document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape'}))");

    Resize(host, window, 1920, 1080, WebViewSettings::Dock::Right);
    PushAboutUpdate(host, L"Right", L"zh-TW", L"current", L"v1.2.5.3-holybear",
        false, false, false, false, L"");
    const size_t sizeBeforeReturn = messages.size();
    Script(view, L"document.querySelector('.rail-item[data-tab=\"pane-video\"]').click()");
    Check(PumpUntil([&] { return Script(view, L"document.documentElement.dataset.activePane === 'pane-video' && "
        L"getComputedStyle(document.querySelector('.metabar')).display === 'grid'") == L"true"; }),
        "leaving About restores the shared signal strip");
    Check(messages.size() == sizeBeforeReturn && Script(view, SharedValues) == settingsBefore &&
        Script(view, L"document.querySelector('#hdmi-source-trigger').isConnected && "
            L"document.querySelector('#slider-volume').isConnected && "
            L"document.querySelector('#pane-shortcuts').isConnected") == L"true",
        "About navigation preserves settings, capture source controls, audio and shortcuts without reinitialization actions");
}
}

// Exercises the production controller and window lifecycle. This hidden,
// software-rendered fixture cannot prove the compositor's visible pixels.
void TestF1Visibility(WebViewSettings& host, HWND window) {
    auto* controller = WebViewSettingsTestAccess::Controller(host);
    const auto bounds = [&] { RECT rc{}; Hr(controller->get_Bounds(&rc)); return rc; };
    const auto collapsed = [&] { const RECT rc = bounds(); return IsRectEmpty(&rc) != FALSE; };
    Check(!host.IsVisible() && collapsed(), "new hidden controller starts collapsed");
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"Intermediate D3D Window";
    const ATOM registered = RegisterClassW(&cls);
    Check(registered != 0, "test-only browser surface class registered");
    HWND surface = CreateWindowExW(0, cls.lpszClassName, L"fixture surface", WS_CHILD | WS_VISIBLE,
        0, 0, 20, 20, window, nullptr, cls.hInstance, nullptr);
    HWND unrelated = CreateWindowExW(0, L"STATIC", L"unrelated child", WS_CHILD | WS_VISIBLE,
        0, 0, 20, 20, window, nullptr, cls.hInstance, nullptr);
    Check(surface && unrelated, "isolated browser and unrelated child fixtures");
    for (int cycle = 0; cycle < 6; ++cycle) {
        const auto dock = cycle % 3 == 0 ? WebViewSettings::Dock::Full :
            cycle % 3 == 1 ? WebViewSettings::Dock::Left : WebViewSettings::Dock::Right;
        host.SetDock(dock, 420); host.Show(true);
        Check(host.IsVisible() && !collapsed(), "F1 reopens with restored bounds in Full/Left/Right");
        RECT before{}, after{}; GetWindowRect(window, &before);
        host.Show(false); GetWindowRect(window, &after);
        BOOL visible = TRUE; Hr(controller->get_IsVisible(&visible));
        Check(!visible && !host.IsVisible() && collapsed() && EqualRect(&before, &after),
            "F1 close collapses the controller and composition refresh preserves parent geometry");
        Check(!(GetWindowLongW(surface, GWL_STYLE) & WS_VISIBLE) &&
            (GetWindowLongW(unrelated, GWL_STYLE) & WS_VISIBLE), "only browser drawing surfaces are hidden");
        const size_t retained = WebViewSettingsTestAccess::HiddenSurfaces(host);
        host.Show(false);
        Check(retained > 0 && WebViewSettingsTestAccess::HiddenSurfaces(host) >= retained,
            "repeated close retains asynchronously hidden surfaces for the next show");
        SetWindowPos(window, nullptr, 0, 0, 800 + cycle * 30, 600 + cycle * 20,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        host.Resize(); host.SetDock(WebViewSettings::Dock::Full, 420);
        Check(collapsed(), "resize and dock changes cannot expand a hidden controller");
        host.SetDock(dock, 420); host.Show(true);
        RECT client{}; GetClientRect(window, &client); const RECT actual = bounds();
        const LONG strip = (std::min<LONG>)(client.right, MulDiv(420, GetDpiForWindow(window), 96));
        Check(actual.bottom == client.bottom && actual.top == 0 &&
            (dock == WebViewSettings::Dock::Full ? EqualRect(&actual, &client) :
                actual.right - actual.left == strip && (dock == WebViewSettings::Dock::Left ?
                    actual.left == 0 : actual.right == client.right)),
            "reopened bounds follow resized parent and selected dock");
        Check((GetWindowLongW(surface, GWL_STYLE) & WS_VISIBLE) &&
            WebViewSettingsTestAccess::HiddenSurfaces(host) == 0, "show restores tracked surfaces once");
    }
    MONITORINFO monitor{sizeof(monitor)};
    Check(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor), "monitor fixture bounds");
    SetWindowPos(window, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
        monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    host.Show(false); RECT full{}; GetWindowRect(window, &full);
    Check(EqualRect(&full, &monitor.rcMonitor) && collapsed(), "fullscreen close stays collapsed without moving the window");
    DestroyWindow(surface);
    HWND replacement = CreateWindowExW(0, L"STATIC", L"replacement", WS_CHILD,
        0, 0, 20, 20, window, nullptr, cls.hInstance, nullptr);
    host.Show(true);
    Check(!(GetWindowLongW(replacement, GWL_STYLE) & WS_VISIBLE), "destroyed browser HWND cannot restore an unrelated replacement");
    DestroyWindow(replacement);
    surface = CreateWindowExW(0, cls.lpszClassName, L"reparented", WS_CHILD | WS_VISIBLE,
        0, 0, 20, 20, window, nullptr, cls.hInstance, nullptr);
    HWND otherParent = CreateWindowExW(0, L"STATIC", L"other parent", WS_POPUP,
        0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
    host.Show(false); SetParent(surface, otherParent); host.Show(true);
    Check(!(GetWindowLongW(surface, GWL_STYLE) & WS_VISIBLE), "reparented browser surface is never restored");
    DestroyWindow(otherParent); DestroyWindow(unrelated);
    host.Show(false); host.Shutdown();
    Check(WebViewSettingsTestAccess::HiddenSurfaces(host) == 0 && !host.IsVisible(),
        "shutdown forgets old-process surface handles before a restart");
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
}

void TestGameControlsAndPanelResize(WebViewSettings& host,HWND window,std::vector<std::wstring>& messages){
    auto* view=WebViewSettingsTestAccess::View(host);
    host.SetMessageHandler([&](const std::wstring& json){
        messages.push_back(json);const auto message=NitLink::ParseSettingsMessage(json);
        if(message&&message->action==L"setPanelWidth"){
            const auto width=static_cast<int>(message->number);host.SetDock(host.GetDock(),width);
            host.PostMessage(L"{\"state\":{\"panelWidth\":"+std::to_wstring(width)+L"}}");
        }
    });
    for(const auto dock:{WebViewSettings::Dock::Left,WebViewSettings::Dock::Right}){
        const auto* side=dock==WebViewSettings::Dock::Left?L"Left":L"Right";
        Resize(host,window,1280,800,dock,500);PushState(host,side,L"zh-TW");
        host.PostMessage(LR"({"state":{"manualHdmiSource":"switch2","effectiveHdmiSource":"Switch 2"}})");
        host.PostMessage(LR"({"accountLibrary":{"accounts":[],"games":[]}})");
        Script(view,L"document.querySelector('[data-tab=\"pane-games\"]').click()");
        Check(PumpUntil([&]{return Script(view,LR"JS(document.getElementById('account-overview').open&&document.getElementById('account-unlinked').hidden&&document.querySelectorAll('#account-linked-grid .account-card').length===3&&[...document.querySelectorAll('.account-card')].every(card=>card.getBoundingClientRect().height>0))JS")==L"true";}),"all platforms initially expand when no account is linked");
        Script(view,L"document.querySelector('#account-overview > summary').click();NitLinkAccountLibrary.setLocale('zh-TW')");
        Check(Script(view,LR"JS(!document.getElementById('account-overview').open&&[...document.querySelectorAll('.account-card')].every(card=>!card.checkVisibility()))JS")==L"true","users can collapse the unlinked overview and rendering preserves their choice");
        Script(view,L"document.querySelector('#account-overview > summary').click()");
        host.PostMessage(LR"({"accountLibrary":{"preview":false,"accounts":[{"provider":"nintendo","accountId":"fixture","status":"connected"}],"games":[{"key":"nintendo-BEE-0400000000000002","id":700001,"provider":"nintendo","platform":"Switch 2","name":"控制測試 DEMO","nameLocale":"zh-TW","totalMinutes":120,"lastPlayed":"2026-10-31T12:00:00Z","artworks":[],"displayNames":{"zh-TW":"控制測試 DEMO"}},{"key":"nintendo-BEE-0400000000000001","id":700001,"provider":"nintendo","platform":"Switch 2","name":"控制測試","nameLocale":"zh-TW","totalMinutes":60,"lastPlayed":"2026-09-01T12:00:00Z","artworks":[],"displayNames":{"zh-TW":"控制測試"}}]}})");
        host.PostMessage(LR"({"gameState":{"key":"nintendo-BEE-0400000000000001","id":700001,"name":"控制測試","displayName":"控制測試","locale":"zh-TW","artworkPlatform":"switch","coverId":0,"manualCover":false,"finalImageKey":"","dataMode":"real","discordActivityState":"遊玩總時數：1 小時 0 分鐘"}})");
        Script(view,L"document.querySelector('[data-tab=\"pane-games\"]').click()");
        Check(PumpUntil([&]{return Script(view,LR"JS(!document.getElementById('account-unlinked').hidden&&!document.getElementById('account-unlinked').open&&document.querySelectorAll('#account-linked-grid .account-card').length===1&&document.getElementById('account-nintendo').checkVisibility()&&!document.getElementById('account-playstation').checkVisibility()&&!document.getElementById('account-xbox').checkVisibility())JS")==L"true";}),"linking Nintendo leaves it visible and automatically collapses unlinked platforms");
        Script(view,L"document.querySelector('#account-unlinked > summary').click();NitLinkAccountLibrary.setLocale('zh-TW')");
        Check(Script(view,LR"JS(document.getElementById('account-unlinked').open&&document.getElementById('account-playstation').getBoundingClientRect().height>0&&document.getElementById('account-xbox').getBoundingClientRect().height>0)JS")==L"true","users can expand the remaining platforms and normal updates do not close them");
        Script(view,L"document.querySelector('#account-unlinked > summary').click()");
        Check(PumpUntil([&]{return Script(view,LR"JS((()=>{
            const tabs=[...document.querySelectorAll('#account-source-tabs button')];const first=tabs[0].getBoundingClientRect();
            return tabs.length===6&&tabs.every(button=>Math.abs(button.getBoundingClientRect().top-first.top)<1)&&
                [...document.querySelectorAll('#account-source-tabs img')].every(image=>image.complete&&image.naturalWidth>0);
        })())JS")==L"true";},3000),"Xbox and all library filters fit on the first row in both default dock positions");
        Check(PumpUntil([&]{return Script(view,LR"JS((()=>{
            const ids=['game-save-quality','account-match-game','game-adjust-cover','game-reset-all'];
            const buttons=ids.map(id=>document.getElementById(id));const first=buttons[0].getBoundingClientRect();
            return !buttons[1].hidden&&buttons.every(button=>Math.abs(button.getBoundingClientRect().top-first.top)<1&&button.scrollWidth<=button.clientWidth&&getComputedStyle(button).textAlign==='center')&&document.documentElement.scrollWidth<=innerWidth;
        })())JS")==L"true";},3000),"both 500px docks keep all four second-row actions centered and unclipped");
        Check(Script(view,LR"JS([...document.querySelectorAll('[data-cover-provider]')].map(button=>button.dataset.coverProvider).join(',')==='nintendo,community,all'&&document.querySelector('[data-cover-provider=nintendo]').getAttribute('aria-pressed')==='true'&&!document.getElementById('account-match-cancel')&&[...document.querySelectorAll('[data-account-cover-prompt]')].every(node=>node.hidden)&&document.getElementById('game-discord-signal').textContent==='遊玩總時數：1 小時 0 分鐘')JS")==L"true","official-first controls, connected-source prompts and time-only preview match native state");
        Check(Script(view,LR"JS((()=>{const cards=[...document.querySelectorAll('.account-game-title')];return cards.length===2&&cards[0].textContent==='控制測試'&&/DEMO/.test(cards[1].textContent)&&NitLinkGameTitles.coverQuery('Sonic Racing: CrossWorlds - Demo')==='Sonic Racing: CrossWorlds'&&NitLinkGameTitles.coverQuery('Majogami Demo ver.')==='Majogami';})())JS")==L"true","demo records stay separate, sort last and use the base name only for artwork lookup");
        Script(view,L"window._updateGameLive({manualHdmiSource:'ps5',effectiveHdmiSource:'PS5'})");
        Check(Script(view,LR"JS(document.getElementById('game-store-search').hidden&&[...document.querySelectorAll('[data-account-cover-prompt]')].every(node=>!node.hidden&&node.querySelectorAll('button').length===1&&node.textContent.includes('PlayStation')))JS")==L"true","an unlinked selected console shows only its own link prompt and hides Nintendo store search");
        Script(view,L"window._updateGameLive({manualHdmiSource:'switch2',effectiveHdmiSource:'Switch 2'});document.getElementById('game-query').value='控制測試 DEMO';document.getElementById('game-search').click()");
        uint32_t searchRequest=0;
        Check(PumpUntil([&]{for(auto it=messages.rbegin();it!=messages.rend();++it){const auto message=NitLink::ParseSettingsMessage(*it);if(message&&message->action==L"searchGames"){searchRequest=message->request;return true;}}return false;}),"demo query reaches the native bridge");
        host.PostMessage(L"{\"gameEvent\":{\"kind\":\"search\",\"request\":"+std::to_wstring(searchRequest)+L",\"locale\":\"zh-TW\",\"status\":\"upstreamUnavailable\",\"games\":[]}}");
        Check(PumpUntil([&]{return Script(view,LR"JS(document.querySelectorAll('.game-result').length===2&&document.getElementById('game-search-status').dataset.error==='true'&&document.getElementById('game-search-empty').hidden)JS")==L"true";}),"linked-platform fallback cards stay usable while the public catalogue error is shown once");
        Script(view,L"document.getElementById('panel-resize').focus()");
        BrowserKey(view,dock==WebViewSettings::Dock::Left?L"ArrowRight":L"ArrowLeft",dock==WebViewSettings::Dock::Left?L"ArrowRight":L"ArrowLeft",dock==WebViewSettings::Dock::Left?VK_RIGHT:VK_LEFT);
        Check(PumpUntil([&]{return Script(view,L"document.getElementById('panel-resize').getAttribute('aria-valuenow')")==L"\"516\"";}),"dock width keyboard control reaches the native host");
        BrowserKey(view,L"Home",L"Home",VK_HOME);
        Check(PumpUntil([&]{return Script(view,L"document.getElementById('panel-resize').getAttribute('aria-valuenow')")==L"\"500\"";}),"dock width cannot shrink below its default");
        const int start=dock==WebViewSettings::Dock::Left?496:4;const int finish=start+(dock==WebViewSettings::Dock::Left?40:-40);
        Script(view,L"window.__resizeStarted=false;document.getElementById('panel-resize').addEventListener('pointerdown',()=>window.__resizeStarted=true,{once:true})");
        BrowserCommand(view,L"Input.dispatchMouseEvent",L"{\"type\":\"mousePressed\",\"x\":"+std::to_wstring(start)+L",\"y\":240,\"button\":\"left\",\"buttons\":1,\"clickCount\":1}");
        Check(PumpUntil([&]{return Script(view,L"window.__resizeStarted")==L"true";}),"inner dock edge receives the drag start");
        BrowserCommand(view,L"Input.dispatchMouseEvent",L"{\"type\":\"mouseMoved\",\"x\":"+std::to_wstring(finish)+L",\"y\":240,\"button\":\"left\",\"buttons\":1}");
        Check(PumpUntil([&]{return Script(view,L"Number(document.getElementById('panel-resize').getAttribute('aria-valuenow'))>=530")==L"true";}),"dragging either inner dock edge resizes the actual native controller");
        BrowserCommand(view,L"Input.dispatchMouseEvent",L"{\"type\":\"mouseReleased\",\"x\":"+std::to_wstring(finish)+L",\"y\":240,\"button\":\"left\",\"buttons\":0,\"clickCount\":1}");
    }
    host.SetMessageHandler([&messages](const std::wstring& json){messages.push_back(json);});
    host.Show(false);
}

int main(int argc, char** argv) {
    const bool captureOnly = argc == 3 && std::string(argv[1]) == "--capture-layout";
    const bool captureUpdates = argc == 3 && std::string(argv[1]) == "--capture-updates";
    const bool captureAudio = argc == 3 && std::string(argv[1]) == "--capture-audio";
    const bool captureContributors = argc == 3 && std::string(argv[1]) == "--capture-contributors";
    const bool visibilityOnly = argc == 2 && std::string(argv[1]) == "--visibility-only";
    const auto profile = std::filesystem::temp_directory_path() /
        (L"NitLink-Settings-layout-test-" + std::to_wstring(GetCurrentProcessId()));
    SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", profile.c_str());
    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
        L"--disable-gpu --disable-gpu-compositing --disable-background-networking");
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    HWND window = CreateWindowExW(0, L"STATIC", L"NitLink settings layout test", WS_POPUP,
        0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    try {
        Check(window != nullptr, "isolated hidden test window created");
        WebViewSettings host;
        std::vector<std::wstring> messages;
        host.SetMessageHandler([&](const std::wstring& json) {
            const auto message=NitLink::ParseSettingsMessage(json);
            if(message&&message->action==L"setShortcutContext"){
                host.PostMessage(L"{\"shortcutContext\":"+std::to_wstring(static_cast<unsigned>(message->number))+L"}");
                return;
            }
            messages.push_back(json);
        });
        Check(host.Initialize(window, 800, 600), "native settings host starts");
        host.NavigateToMenu();
        Check(PumpUntil([&] { return host.IsReady(); }, 15000), "packaged page is ready through the real native bridge");
        auto controller3 = wil::com_ptr<ICoreWebView2Controller>(WebViewSettingsTestAccess::Controller(host))
            .try_query<ICoreWebView2Controller3>();
        Check(controller3 != nullptr, "runtime supports deterministic raster scale");
        Hr(controller3->put_ShouldDetectMonitorScaleChanges(FALSE));
        Hr(controller3->put_RasterizationScale(1.0));
        Hr(controller3->put_ZoomFactor(1.0));
        auto* view = WebViewSettingsTestAccess::View(host);
        std::wcout << L"Initial page locale and browser languages: " <<
            Script(view, L"[document.documentElement.lang, navigator.languages]") << L'\n';
        Script(view, L"window.__fontsReady = false; document.fonts.ready.then(() => __fontsReady = true)");
        Check(PumpUntil([&] { return Script(view, L"window.__fontsReady") == L"true"; }), "bundled fonts loaded");
        if (visibilityOnly) {TestGameControlsAndPanelResize(host,window,messages);TestF1Visibility(host, window);}
        else if (captureOnly) CaptureLayouts(host, window, std::filesystem::path(argv[2]));
        else if (captureUpdates) TestUpdateFlow(host, window, messages, std::filesystem::path(argv[2]));
        else if (captureAudio) {
            const std::filesystem::path folder(argv[2]);
            std::filesystem::create_directories(folder);
            for (const auto side : {L"Full",L"Right"}) {
                const bool full = std::wstring_view(side) == L"Full";
                Resize(host,window,full?1100:1920,full?760:1080,
                    full?WebViewSettings::Dock::Full:WebViewSettings::Dock::Right,500);
                PushState(host,side,L"zh-TW");
                Script(view,L"document.querySelector('.rail-item[data-tab=\"pane-audio\"]').click(); "
                    L"if(!document.getElementById('audio-debug').checked)document.getElementById('audio-debug').click()");
                host.PostMessage(LR"({"state":{"locale":"zh-TW","panelEffect":"blur",
                    "audioStreaming":true,"audioPlaybackState":"playing","audioAdaptive":true,"audioDrift":true,
                    "audioExclusiveActive":false,"audioFillMs":12,"audioQueueMs":11,"audioRenderMs":10,
                    "audioEffectiveMs":11,"audioAverageMs":12.73,"audioMinMs":12,"audioMaxMs":22,
                    "audioPeriodUs":10000,"audioPpm":0,"audioEstimatedPpm":0,"audioClockState":3,
                    "audioClockConfidence":0,"audioUnderrunEvents":0,"audioLocalReprimes":0,"audioReloads":0,
                    "audioUnderruns":0,"audioOverruns":0,"audioResyncs":1,"audioStartupSilence":1488,
                    "audioRebufferFrames":0,"audioResetDiscardFrames":0,"audioTraceAvailable":true,
                    "audioEndpointInfo":"Native shared low-period | native min 10.000000 ms, default 10.000000 ms | actual 10 ms | capture 48000 Hz, 2ch, 32-bit -> stream 48000 Hz, 2ch, 32-bit"}})");
                Check(PumpUntil([&] {return Script(view,L"document.getElementById('audio-health').textContent.includes('48000 Hz')")==L"true";}),
                    "audio preview renders the complete synthetic diagnostics");
                Script(view,L"document.getElementById('audio-health').scrollIntoView({block:'center'})");
                PumpUntil([] {return false;},150);
                Capture(view,folder/(full?L"audio-settings-window.png":L"audio-settings-right.png"));
            }
        }
        else if (captureContributors) TestAbout(host, window, messages, std::filesystem::path(argv[2]));
        else {
            TestLayouts(host, window, messages);
            TestAbout(host, window, messages);
            TestUpdateFlow(host, window, messages);
            TestAudioDiagnostics(host, messages);
            TestAudioRefresh(host, window, messages);
        }
        host.Shutdown();
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        result = 1;
    }
    if (window) DestroyWindow(window);
    CoUninitialize();
    std::wcout << L"Disposable profile: " << profile.c_str() << L'\n';
    return result;
}
