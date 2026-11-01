/*
 * NitLink: Console games as native PC apps.
 *
 * Lightweight capture card viewer with shader pipeline,
 * image upscaling, and per-game presets.
 *
 * MIT License, https://github.com/nitlink-dev/nitlink
 */

#include "app/application.h"
#include "app/localization.h"
#include "app/nintendo_auth_callback.h"
#include <windows.h>
#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow)
{
    // Runtime dependencies resolve only beside the executable or in System32.
    // A failed policy setup must not silently restore working-directory lookup.
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        MessageBoxW(nullptr, L"Unable to configure secure library loading.", L"NitLink", MB_ICONERROR);
        return 1;
    }

    if(const auto callback=NitLink::Accounts::ForwardNintendoAuthCallback())return *callback;

    // Opt into per-monitor DPI awareness BEFORE creating any windows.
    // Without this, Windows lies about pixel sizes when DPI scaling is
    // not 100%, which produces a blurry image because rendering happens at
    // a lower resolution than the actual screen.
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    // COM init: single-threaded apartment.
    //
    // Earlier builds used COINIT_MULTITHREADED (MTA) because Media Foundation,
    // DXGI, and WASAPI all work fine in MTA. WebView2 requires STA on the
    // thread that creates the environment/controller: calling
    // CreateCoreWebView2EnvironmentWithOptions from an MTA thread returns
    // RPC_E_CHANGED_MODE (0x80010106) and the async callback never fires.
    //
    // STA is the path of least surprise for a Windows app with a UI thread.
    // MF and WASAPI marshal cross-apartment internally and keep working.
    // Background threads owned by NitLink that call back into COM need
    // their own CoInitializeEx.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, NitLink::Localization::Instance().Get(L"error.com").c_str(),
                    L"NitLink", MB_ICONERROR);
        return 1;
    }

    // Start Media Foundation
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, NitLink::Localization::Instance().Get(L"error.mediaFoundation").c_str(),
                    L"NitLink", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    int exitCode = 0;
    {
        NitLink::Application app;
        
        if (!app.Initialize(hInstance, nCmdShow)) {
            MessageBoxW(nullptr, NitLink::Localization::Instance().Get(L"error.application").c_str(),
                        L"NitLink", MB_ICONERROR);
            exitCode = 1;
        } else {
            app.Run();
        }
    }

    // Application teardown can release COM objects even after initialization
    // fails, so the platform services outlive the application in both paths.
    if (FAILED(MFShutdown())) exitCode = 1;
    CoUninitialize();
    return exitCode;
}
