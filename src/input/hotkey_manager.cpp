#include "hotkey_manager.h"

namespace NitLink {
HotkeyManager::HotkeyManager(HWND hwnd) : m_hwnd(hwnd) {}
HotkeyManager::~HotkeyManager() = default;

void HotkeyManager::Register(ShortcutAction action, Callback callback, bool repeat) {
    Hotkey hotkey;
    hotkey.action=action;
    hotkey.keys=DefaultShortcutBindings[static_cast<size_t>(action)];
    hotkey.callback=std::move(callback);
    hotkey.repeat=repeat;
    m_hotkeys.push_back(std::move(hotkey));
}

void HotkeyManager::ApplyBindings(const ShortcutBindings& bindings) {
    for(auto& hotkey:m_hotkeys){
        hotkey.keys=bindings[static_cast<size_t>(hotkey.action)];
        // Saving a held combination must not execute the newly assigned action.
        hotkey.wasPressed=true;hotkey.nextRepeatAt=~ULONGLONG{0};
    }
}

void HotkeyManager::Poll(ShortcutInputContext context) {
    if(GetForegroundWindow()!=m_hwnd){
        for(auto& hotkey:m_hotkeys)hotkey.wasPressed=false;
        return;
    }
    const unsigned modifiers=((GetAsyncKeyState(VK_CONTROL)&0x8000)?1u:0u)|
        ((GetAsyncKeyState(VK_MENU)&0x8000)?2u:0u)|((GetAsyncKeyState(VK_SHIFT)&0x8000)?4u:0u);
    const bool windowsKey=(GetAsyncKeyState(VK_LWIN)&0x8000)||(GetAsyncKeyState(VK_RWIN)&0x8000);
    const auto now=GetTickCount64();
    for(auto& hotkey:m_hotkeys){
        const auto [key,required]=hotkey.keys;
        const bool settingsKey=hotkey.action==ShortcutAction::toggle_settings&&
            (required||(key>=VK_F1&&key<=VK_F24));
        if(context==ShortcutInputContext::Recording||(context==ShortcutInputContext::TextInput&&!settingsKey)){
            hotkey.wasPressed=true;hotkey.nextRepeatAt=~ULONGLONG{0};continue;
        }
        const bool pressed=!windowsKey&&modifiers==required&&(GetAsyncKeyState(static_cast<int>(key))&0x8000);
        if(pressed&&(!hotkey.wasPressed||(hotkey.repeat&&now>=hotkey.nextRepeatAt))){
            hotkey.nextRepeatAt=now+(hotkey.wasPressed?50:300);
            hotkey.callback();
        }
        hotkey.wasPressed=pressed;
    }
}
}
