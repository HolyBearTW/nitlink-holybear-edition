#include "app/config.h"
#include "renderer/window.h"
#include <objbase.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
    std::pair<int32_t, int32_t> expectedPosition{};
    const auto instance = GetModuleHandleW(nullptr);
    {
        Window window;
        Window::Desc desc;
        desc.width = 640;
        desc.height = 360;
        Require(window.Create(instance, desc), "Window creates");
        RECT initial{}; MONITORINFO primary{sizeof(primary)};
        Require(GetWindowRect(window.GetHWND(), &initial) &&
                    GetMonitorInfoW(MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY), &primary),
                "Initial window and primary work area are available");
        Require(std::abs(initial.left + initial.right - primary.rcWork.left - primary.rcWork.right) <= 1 &&
                    std::abs(initial.top + initial.bottom - primary.rcWork.top - primary.rcWork.bottom) <= 1,
                "A window without saved coordinates starts at the center of the work area");
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
        RECT bounds{};
        Require(GetWindowRect(window.GetHWND(), &bounds) != FALSE, "Window screen coordinates are available");
        MONITORINFO monitor{sizeof(monitor)};
        Require(GetMonitorInfoW(MonitorFromWindow(window.GetHWND(), MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
                "The window monitor work area is available");
        expectedPosition = {
            monitor.rcWork.left + std::max<LONG>(0, (monitor.rcWork.right - monitor.rcWork.left - (bounds.right - bounds.left)) / 2),
            monitor.rcWork.top + std::max<LONG>(0, (monitor.rcWork.bottom - monitor.rcWork.top - (bounds.bottom - bounds.top)) / 2)};
        Require(SetWindowPos(window.GetHWND(), nullptr, expectedPosition.first, expectedPosition.second, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "Move places the window at its intended screen coordinates");
        Require(window.GetWindowedPosition() == expectedPosition, "A normal move updates the remembered position");

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
        Require(window.GetWindowedPosition() == expectedPosition, "The last normal screen position survives close");
        Config config;
        const auto [width, height] = window.GetWindowedClientSize();
        config.windowWidth = width;
        config.windowHeight = height;
        config.windowPosition = window.GetWindowedPosition();
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
        desc.position = loaded.windowPosition;
        Require(reopened.Create(instance, desc), "Window reopens from saved dimensions");
        Require(reopened.GetClientSize() == expected, "Reopened client dimensions match the last normal size");
        RECT reopenedBounds{};
        Require(GetWindowRect(reopened.GetHWND(), &reopenedBounds) != FALSE &&
                    reopenedBounds.left == expectedPosition.first && reopenedBounds.top == expectedPosition.second,
                "Reopened native window returns to the saved screen position");
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
        RECT client{};GetClientRect(main.GetHWND(),&client);
        const int captionY=main.GetContentBounds().top/2;
        std::vector<POINT> captionButtons;
        int buttonStart=-1;
        for(int x=0;x<=client.right;++x){
            POINT point{x,captionY};ClientToScreen(main.GetHWND(),&point);
            const bool button=x<client.right&&SendMessageW(main.GetHWND(),WM_NCHITTEST,0,MAKELPARAM(point.x,point.y))==HTCLIENT;
            if(button&&buttonStart<0)buttonStart=x;
            if(!button&&buttonStart>=0){captionButtons.push_back({(buttonStart+x-1)/2,captionY});buttonStart=-1;}
        }
        Require(captionButtons.size()==5,"Native hit testing exposes all five caption buttons");
        const POINT gear=captionButtons[1];
        for(const bool open:{false,true}){
            main.SetChromeState(0,false,open);
            for(size_t button=0;button<captionButtons.size();++button){
                const POINT center=captionButtons[button];
                const DWORD position=GetMessagePos();
                SetWindowPos(main.GetHWND(),nullptr,static_cast<short>(LOWORD(position))-center.x,
                    static_cast<short>(HIWORD(position))-center.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
                const auto result=SendMessageW(main.GetHWND(),WM_MOUSEACTIVATE,
                    reinterpret_cast<WPARAM>(main.GetHWND()),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
                Require(result==(open&&button==1?MA_NOACTIVATE:MA_ACTIVATE),
                    "Only the closing gear keeps activation; opening and other caption buttons activate normally");
            }
        }
        settings.Show(SW_SHOWNOACTIVATE);
        unsigned toggles=0,closes=0;
        settings.SetCloseCallback([&](){++closes;main.SetChromeState(0,false,false);});
        Window::InputCallbacks callbacks;
        callbacks.onSettings=[&](){++toggles;SendMessageW(settings.GetHWND(),WM_SYSCOMMAND,SC_CLOSE,0);};
        main.SetInputCallbacks(std::move(callbacks));
        SendMessageW(main.GetHWND(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(gear.x,gear.y));
        SendMessageW(main.GetHWND(),WM_LBUTTONUP,0,MAKELPARAM(gear.x,gear.y));
        const auto deadline=GetTickCount64()+2000;
        while(IsWindowVisible(settings.GetHWND())&&GetTickCount64()<deadline){
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
        }
        Require(toggles==1&&closes==1&&!IsWindowVisible(settings.GetHWND()),
            "A gear click is delivered once and finishes the settings close fade once");
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
        {
            Window offscreen;
            Window::Desc desc;
            desc.width = 640; desc.height = 360; desc.position = {{100000, 100000}};
            Require(offscreen.Create(GetModuleHandleW(nullptr), desc), "Saved offscreen window creates");
            RECT bounds{}; GetWindowRect(offscreen.GetHWND(), &bounds);
            MONITORINFO monitor{sizeof(monitor)};
            Require(GetMonitorInfoW(MonitorFromWindow(offscreen.GetHWND(), MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
                    "Restored window has an available monitor");
            Require(bounds.left >= monitor.rcWork.left && bounds.top >= monitor.rcWork.top &&
                        bounds.right <= monitor.rcWork.right && bounds.bottom <= monitor.rcWork.bottom,
                    "A position from a disconnected monitor restores within an available work area");
            SendMessageW(offscreen.GetHWND(), WM_CLOSE, 0, 0);

            Window partial;
            desc.position = {{monitor.rcWork.left - 60, monitor.rcWork.top + 40}};
            Require(partial.Create(GetModuleHandleW(nullptr), desc), "Partially offscreen saved window creates");
            Require(GetWindowRect(partial.GetHWND(), &bounds) != FALSE &&
                        bounds.left == desc.position->first && bounds.top == desc.position->second,
                    "A visible window keeps its exact position instead of being snapped inside one monitor");
            SendMessageW(partial.GetHWND(), WM_CLOSE, 0, 0);
        }
        std::cout << "PASS Window size and position survive move, close and reopen in all seven window states\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    if (!configPath.empty()) std::filesystem::remove(configPath, ignored);
    CoUninitialize();
    return result;
}
