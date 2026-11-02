#include "app/config.h"
#include "renderer/window.h"
#include <objbase.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using NitLink::Config;
using NitLink::Window;

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void ResizeClient(Window& window, uint32_t width, uint32_t height)
{
    // Request picture dimensions: the custom caption is outside the picture,
    // and Windows' default frame calculation does not describe this window.
    RECT outer{};
    Require(GetWindowRect(window.GetHWND(), &outer) != FALSE, "Window dimensions are available");
    const auto [pictureWidth,pictureHeight]=window.GetClientSize();
    Require(SetWindowPos(window.GetHWND(), nullptr, 0, 0,
                         static_cast<int>(width)+outer.right-outer.left-static_cast<int>(pictureWidth),
                         static_cast<int>(height)+outer.bottom-outer.top-static_cast<int>(pictureHeight),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
            "Client resize succeeds");
}

enum class CloseState { Normal, Minimized, Maximized, Fullscreen, PiP, FullscreenRoundTrip, PiPRoundTrip };

static void CheckClose(CloseState state, const std::filesystem::path& configPath)
{
    constexpr std::pair<uint32_t, uint32_t> expected{1000, 600};
    const auto instance = GetModuleHandleW(nullptr);
    {
        Window window;
        Window::Desc desc;
        desc.width = 640;
        desc.height = 360;
        Require(window.Create(instance, desc), "Window creates");
        Require(window.GetClientSize() == std::pair<uint32_t, uint32_t>{640, 360},
                "The reserved custom caption does not consume picture dimensions");
        window.AcknowledgeDisplayChange();
        window.AcknowledgeMove();
        SendMessageW(window.GetHWND(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
        Require(window.WasDisplayChanged() && !window.WasMoved(),
                "Display-mode changes invalidate refresh metadata without inventing a move");
        window.AcknowledgeDisplayChange();
        Require(!window.WasDisplayChanged(), "Display-change notifications acknowledge once");
        SendMessageW(window.GetHWND(), WM_MOVE, 0, MAKELPARAM(100, 100));
        Require(window.WasMoved() && !window.WasDisplayChanged(),
                "Same-size moves schedule monitor-crossing detection");
        window.AcknowledgeMove();
        Require(!window.WasMoved(), "Move notifications acknowledge once");
        ResizeClient(window, 800, 500);
        ResizeClient(window, expected.first, expected.second);
        Require(window.GetClientSize() == expected, "Resize produces the requested client dimensions");

        switch (state) {
        case CloseState::Minimized:
            ShowWindow(window.GetHWND(), SW_MINIMIZE);
            Require(IsIconic(window.GetHWND()) != FALSE, "Window minimizes");
            break;
        case CloseState::Maximized:
            ShowWindow(window.GetHWND(), SW_MAXIMIZE);
            Require(IsZoomed(window.GetHWND()) != FALSE, "Window maximizes");
            break;
        case CloseState::Fullscreen:
        case CloseState::FullscreenRoundTrip:
            window.SetFullscreen(true);
            if (state == CloseState::FullscreenRoundTrip) window.SetFullscreen(false);
            break;
        case CloseState::PiP:
        case CloseState::PiPRoundTrip:
            window.SetPiP(true, 480, 270, 1.0f);
            if (state == CloseState::PiPRoundTrip) window.SetPiP(false, 0, 0, 1.0f);
            break;
        default:
            break;
        }

        const HWND handle = window.GetHWND();
        SendMessageW(handle, WM_CLOSE, 0, 0);
        Require(!IsWindow(handle), "Close destroys the native window before saving");
        Require(window.GetClientSize() == std::pair<uint32_t, uint32_t>{0, 0},
                "The destroyed window no longer supplies client dimensions");
        Require(window.GetWindowedClientSize() == expected, "The last normal client size survives close");
        Config config;
        const auto [width, height] = window.GetWindowedClientSize();
        config.windowWidth = width;
        config.windowHeight = height;
        Require(config.Save(configPath.string()), "Window dimensions save");
    }
    Require(UnregisterClassW(L"NitLinkWindowClass", instance) != FALSE, "Closed window class releases");

    Config loaded;
    Require(loaded.Load(configPath.string()), "Saved dimensions reload");
    {
        Window reopened;
        Window::Desc desc;
        desc.width = loaded.windowWidth;
        desc.height = loaded.windowHeight;
        Require(reopened.Create(instance, desc), "Window reopens from saved dimensions");
        Require(reopened.GetClientSize() == expected, "Reopened client dimensions match the last normal size");
        SendMessageW(reopened.GetHWND(), WM_CLOSE, 0, 0);
    }
    Require(UnregisterClassW(L"NitLinkWindowClass", instance) != FALSE, "Reopened window class releases");
    MSG message{};
    while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}

static void CheckGlassWindows()
{
    const auto instance=GetModuleHandleW(nullptr);
    {
        Window main,settings;
        Window::Desc mainDesc;mainDesc.width=800;mainDesc.height=500;
        Window::Desc settingsDesc;settingsDesc.width=700;settingsDesc.height=500;settingsDesc.settingsWindow=true;
        Require(main.Create(instance,mainDesc)&&settings.Create(instance,settingsDesc),
            "Main and independent settings windows create together");
        for(const auto hwnd:{main.GetHWND(),settings.GetHWND()}){
            Require(GetWindow(hwnd,GW_OWNER)==nullptr,"Both content windows remain unowned taskbar windows");
            Require((GetWindowLongW(hwnd,GWL_EXSTYLE)&WS_EX_APPWINDOW)!=0,
                "Both content windows join the application's taskbar group");
        }
        main.SetGlassStatus(Window::GlassStatus{L"No signal",L"Waiting for source",L"Check HDMI",L"Waiting"});
        settings.SetPanelBackdrop(settings.GetContentBounds());
        main.Show(SW_SHOWNOACTIVATE);settings.Show(SW_SHOWNOACTIVATE);
        if(FindWindowW(L"NitLinkGlassBackdrop",nullptr)){
            for(const auto hwnd:{main.GetHWND(),settings.GetHWND()}){
                const auto glass=GetWindow(hwnd,GW_HWNDNEXT);
                wchar_t name[64]{};if(glass)GetClassNameW(glass,name,64);
                Require(std::wstring_view(name)==L"NitLinkGlassBackdrop"&&IsWindowVisible(glass),
                    "Each visible window has its glass directly behind it");
                RECT before{},after{};GetWindowRect(glass,&before);
                RECT host{};GetWindowRect(hwnd,&host);
                Require(SetWindowPos(hwnd,nullptr,host.left+42,host.top+23,0,0,
                    SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"Window moves");
                GetWindowRect(glass,&after);
                Require(after.left-before.left==42&&after.top-before.top==23&&IsWindowVisible(glass),
                    "Glass follows a drag without becoming transparent");
                SendMessageW(hwnd,WM_ACTIVATE,WA_INACTIVE,0);
                Require(IsWindowVisible(glass),"Deactivating the window preserves its glass");
                ShowWindow(hwnd,SW_MINIMIZE);
                Require(IsIconic(hwnd)&&!IsWindowVisible(glass),"Minimizing hides the glass");
                SendMessageW(hwnd,WM_SYSCOMMAND,SC_RESTORE,0);
                Require(!IsIconic(hwnd)&&IsWindowVisible(glass),
                    "Restoring the window restores its glass immediately");
            }
        }else std::cout << "SKIP Glass compositor unavailable on isolated desktop\n";
        SendMessageW(settings.GetHWND(),WM_CLOSE,0,0);
        SendMessageW(main.GetHWND(),WM_CLOSE,0,0);
    }
    Require(UnregisterClassW(L"NitLinkWindowClass",instance)!=FALSE,
        "The independent glass windows release their content class");
}

int main()
{
    // Fullscreen and PiP transitions run on an inactive desktop so checks never cover the user's screen.
    // The test process retains the active desktop handle until process exit releases it.
    const std::wstring name = L"NitLinkWindowSizeTests-" + std::to_wstring(GetCurrentProcessId());
    const HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) {
        std::cerr << "FAIL Could not create an isolated test desktop\n";
        return 1;
    }
    // The real application keeps its STA alive for the lifetime of its UI
    // windows. Do the same when this test repeatedly recreates them.
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))){
        std::cerr << "FAIL Could not initialize the UI apartment\n";
        return 1;
    }
    int result = 0;
    std::filesystem::path configPath;
    try {
        configPath = std::filesystem::temp_directory_path() / (name + L".ini");
        for (auto state : {CloseState::Normal, CloseState::Minimized, CloseState::Maximized,
                           CloseState::Fullscreen, CloseState::PiP,
                           CloseState::FullscreenRoundTrip, CloseState::PiPRoundTrip}) {
            CheckClose(state, configPath);
        }
        CheckGlassWindows();
        std::cout << "PASS Window size survives resize, close and reopen in all seven window states\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    if (!configPath.empty()) std::filesystem::remove(configPath, ignored);
    CoUninitialize();
    return result;
}
