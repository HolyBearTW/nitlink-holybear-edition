#pragma once

#include <windows.h>
#include <string>
#include <utility>
#include <cstdint>
#include <functional>
#include <array>
#include "playback_power.h"

namespace NitLink {

class Window {
public:
    struct Desc {
        std::wstring title  = L"NitLink";
        uint32_t     width  = 1920;
        uint32_t     height = 1080;
        bool         darkMode = true;
    };

    // Input event callbacks -- set by the application to route mouse/keyboard
    // events to the SettingsPanel or other UI consumers. All optional; nullptr
    // means "ignore that event type".
    struct InputCallbacks {
        std::function<void(int x, int y)>           onMouseMove;
        std::function<void(int x, int y)>           onMouseDown;
        std::function<void(int x, int y)>           onMouseUp;
        std::function<void(int delta)>              onMouseWheel;
        std::function<void(int vk)>                 onKeyDown;
        std::function<void()>                       onSettings;
        std::function<void()>                       onCycleHud;
    };
    struct ChromeLabels {
        std::array<std::wstring,3> hud{L"Standard HUD",L"Detailed HUD",L"Compact HUD"};
        std::wstring settings=L"Settings",hidden=L"Hidden",minimize=L"Minimize",maximize=L"Maximize",restore=L"Restore",close=L"Close";
        bool operator==(const ChromeLabels&) const = default;
    };

    explicit Window(PlaybackPowerRequest::SetStateFn setPowerState = ::SetThreadExecutionState);
    ~Window();

    bool Create(HINSTANCE hInstance, const Desc& desc);
    void Show(int nCmdShow);
    void SetTitle(const std::wstring& title);
    void SetIcon(const std::wstring& iconPath);
    void SetFullscreen(bool fullscreen);
    void SetPiP(bool enabled, uint32_t width, uint32_t height, float opacity);
    bool SetPiPOpacity(float opacity);
    void SetPreventSleep(bool enabled);
    void SetVideoAvailable(bool available);
    bool IsPreventingSleep() const { return m_playbackPower.IsActive(); }

    // Sets where SetPiP will position the PiP window the next time it
    // enters PiP mode. Pass -1 / -1 to fall back to the default
    // (bottom-right of the monitor's work area). Persists in member
    // state; Application loads it from Config at startup and re-applies
    // it on every TogglePiP / NudgePiP.
    void SetPreferredPiPPosition(int32_t x, int32_t y) {
        m_pipPreferredX = x;
        m_pipPreferredY = y;
    }

    // Move the PiP window by (dx, dy) pixels, clamped to the current
    // monitor's work area. No-op when not currently in PiP. Returns true
    // if the window was actually moved.
    bool NudgePiP(int dx, int dy);

    // The applied size stays within config bounds and the current work area.
    // Returns false outside PiP, at a size limit, or if placement fails.
    bool ResizePiP(int dw, int dh, bool keepAspect = false);
    void SetPiPResizeCallback(std::function<void()> cb) { m_onPiPResize = std::move(cb); }

    // Read back the PiP window's current top-left screen position so the
    // caller can persist it. Returns false (and leaves outputs untouched)
    // when not currently in PiP.
    bool GetPiPPosition(int32_t& outX, int32_t& outY) const;

    void SetInputCallbacks(InputCallbacks cb) { m_inputCb = std::move(cb); }
    // Synchronous WM_MOVE notifier. Set this to a function that
    // repositions the WebView2 popup (or anything else that must follow
    // the main window's screen position). Called DIRECTLY from inside
    // WndProc on every WM_MOVE (not deferred through the flag below),
    // because during a title-bar drag Windows enters a modal message
    // loop and the application's PeekMessage pump is blocked. The flag
    // wouldn't get consumed until the drag ended, leaving the popup
    // stranded mid-drag.
    void SetMoveCallback(std::function<void()> cb) { m_onMove = std::move(cb); }
    void SetCloseCallback(std::function<void()> cb) { m_onClose = std::move(cb); }

    HWND GetHWND() const { return m_hwnd; }
    HWND GetRenderHWND() const { return m_videoWindow; }
    RECT GetContentBounds() const;
    void SetChromeState(unsigned hudMode, bool hudVisible, bool settingsVisible);
    void SetChromeLabels(ChromeLabels labels);
    std::pair<uint32_t, uint32_t> GetClientSize() const;
    std::pair<uint32_t, uint32_t> GetWindowedClientSize() const { return m_windowedClientSize; }
    bool WasResized() const { return m_wasResized; }
    void AcknowledgeResize() { m_wasResized = false; }
    // Deferred monitor-crossing detection for the present cap. WebView2
    // tracks the owner synchronously through the separate move callback.
    bool WasMoved() const { return m_wasMoved; }
    void AcknowledgeMove() { m_wasMoved = false; }
    bool WasDisplayChanged() const { return m_displayChanged; }
    void AcknowledgeDisplayChange() { m_displayChanged = false; }

    // Window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK VideoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void UpdatePlaybackPower();
    bool GetPiPWorkArea(RECT& work) const;
    bool ApplyPiPRect(const RECT& rect);
    int HitTestPiPResize(POINT screenPoint) const;
    bool BeginPiPResize(POINT screenPoint);
    void UpdatePiPResize(POINT screenPoint);
    void EndPiPResize();
    int CaptionHeight() const;
    RECT CaptionButtonRect(int index) const;
    int CaptionButtonAt(POINT point) const;
    void LayoutContent();
    void PaintCaption(HDC dc);
    void RefreshCaption();
    void UpdateCaptionTooltips();
    void InvokeCaptionButton(int index);

    HWND       m_hwnd = nullptr;
    HWND       m_videoWindow = nullptr, m_captionTooltip = nullptr;
    ULONG_PTR  m_gdiplusToken = 0;
    bool       m_desktopGlass = false;
    unsigned   m_hudMode = 0;
    bool       m_hudVisible = false, m_settingsVisible = false;
    int        m_captionHover = -1, m_captionPressed = -1;
    ChromeLabels m_chromeLabels;
    std::array<std::wstring,5> m_captionTooltips;
    PlaybackPowerRequest m_playbackPower;
    bool m_preventSleep = true;
    bool m_videoAvailable = false;
    HINSTANCE  m_hInstance = nullptr;
    bool       m_isFullscreen = false;
    bool       m_wasResized = false;
    bool       m_wasMoved = false;
    bool       m_displayChanged = false;
    RECT       m_windowedRect{}; // Stored position for fullscreen restore
    DWORD      m_windowedStyle = 0;
    std::pair<uint32_t, uint32_t> m_windowedClientSize{};

    // PiP state
    bool     m_isPiP = false;
    uint32_t m_pipWidth = 480;
    uint32_t m_pipHeight = 270;
    double   m_pipScaleAspect = 0.0;
    int      m_pipResizeEdges = 0;
    RECT     m_pipResizeStart{};
    RECT     m_pipResizeWork{};
    POINT    m_pipResizeMouse{};
    // Preferred top-left for the next SetPiP(enabled=true) call. -1 / -1
    // = use the monitor's bottom-right default. Updated by NudgePiP and
    // SetPreferredPiPPosition.
    int32_t  m_pipPreferredX = -1;
    int32_t  m_pipPreferredY = -1;

    InputCallbacks m_inputCb;
    std::function<void()> m_onClose;
    std::function<void()> m_onMove;
    std::function<void()> m_onPiPResize;
};

} // namespace NitLink
