#pragma once

#include <windows.h>
#include "shortcut_bindings.h"
#include <vector>
#include <functional>

namespace NitLink {

class HotkeyManager {
public:
    using Callback = std::function<void()>;

    HotkeyManager(HWND hwnd);
    ~HotkeyManager();

    void Register(ShortcutAction action, Callback callback,
                  bool repeat = false);
    void ApplyBindings(const ShortcutBindings& bindings);
    void Poll(ShortcutInputContext context = ShortcutInputContext::Normal);
    void SetSettingsWindow(HWND hwnd) { m_settingsWindow=hwnd; }

private:
    struct Hotkey {
        ShortcutAction   action;
        ShortcutChord    keys;
        Callback         callback;
        bool             wasPressed = false;
        bool             repeat = false;
        ULONGLONG        nextRepeatAt = 0;
    };

    HWND m_hwnd;
    HWND m_settingsWindow=nullptr;
    std::vector<Hotkey> m_hotkeys;
};

} // namespace NitLink
