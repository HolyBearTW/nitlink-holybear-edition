#include "window.h"
#include <algorithm>
#include <cmath>
#include <dwmapi.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

namespace NitLink {

namespace {
constexpr int kResizeLeft = 1, kResizeRight = 2, kResizeTop = 4, kResizeBottom = 8;

std::pair<int, int> ScalePiPSize(double width, double aspect, int maxWidth, int maxHeight)
{
    const double minWidth = std::max(80.0, 45.0 * aspect);
    const double limit = std::min(double(maxWidth), maxHeight * aspect);
    if (minWidth > limit) return {0, 0};
    width = std::clamp(width, minWidth, limit);
    return {int(std::clamp(std::lround(width), 80L, long(maxWidth))),
            int(std::clamp(std::lround(width / aspect), 45L, long(maxHeight)))};
}
}

Window::Window(PlaybackPowerRequest::SetStateFn setPowerState)
    : m_playbackPower(setPowerState) {}

Window::~Window()
{
    SetVideoAvailable(false);
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if(m_gdiplusToken)Gdiplus::GdiplusShutdown(m_gdiplusToken);
}

bool Window::Create(HINSTANCE hInstance, const Desc& desc)
{
    m_hInstance = hInstance;
    m_windowedClientSize = {desc.width, desc.height};
    Gdiplus::GdiplusStartupInput graphicsStartup;
    if(Gdiplus::GdiplusStartup(&m_gdiplusToken,&graphicsStartup,nullptr)!=Gdiplus::Ok)return false;

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"NitLinkWindowClass";
    // Load the icon embedded in the .exe via app.rc → IDI_APP_ICON.
    // MAKEINTRESOURCEW(101) matches the IDI_APP_ICON #define in resource.h
    // (the 101 is hardcoded here to avoid pulling resource.h into this file).
    // If the resource is missing for any reason, fall back gracefully to
    // the generic IDI_APPLICATION icon so the app still launches.
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm       = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101),
                                            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!wc.hIconSm) wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    // Calculate window size for desired client area
    RECT rc = {0, 0, (LONG)desc.width, (LONG)desc.height};
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_CAPTION) | WS_CLIPCHILDREN;
    const UINT dpi=GetDpiForSystem();
    rc.bottom+=MulDiv(36,static_cast<int>(dpi),96);
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);

    m_hwnd = CreateWindowExW(
        0,
        L"NitLinkWindowClass",
        desc.title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    WNDCLASSEXW videoClass{};videoClass.cbSize=sizeof(videoClass);videoClass.lpfnWndProc=VideoWndProc;
    videoClass.hInstance=hInstance;videoClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    videoClass.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));videoClass.lpszClassName=L"NitLinkVideoSurface";
    if(!RegisterClassExW(&videoClass))return false;
    const auto content=GetContentBounds();
    m_videoWindow=CreateWindowExW(0,videoClass.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
        content.left,content.top,content.right-content.left,content.bottom-content.top,m_hwnd,nullptr,hInstance,this);
    if(!m_videoWindow)return false;

    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    m_captionTooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,m_hwnd,nullptr,hInstance,nullptr);
    for(int i=0;i<5;++i){TOOLINFOW tool{sizeof(tool)};tool.hwnd=m_hwnd;tool.uId=i+1;tool.uFlags=TTF_SUBCLASS;
        tool.rect=CaptionButtonRect(i);tool.lpszText=const_cast<wchar_t*>(L"");SendMessageW(m_captionTooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
    UpdateCaptionTooltips();

    // Enable dark title bar (Windows 11+)
    if (desc.darkMode) {
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(m_hwnd, 
            20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, 
            &darkMode, sizeof(darkMode));
    }

    // Set rounded corners (Windows 11)
    auto cornerPref = 2; // DWMWCP_ROUNDSMALL
    DwmSetWindowAttribute(m_hwnd, 
        33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, 
        &cornerPref, sizeof(cornerPref));
    int backdrop=3; // DWMSBT_TRANSIENTWINDOW: Windows desktop Acrylic.
    m_desktopGlass=SUCCEEDED(DwmSetWindowAttribute(m_hwnd,38,&backdrop,sizeof(backdrop)));
    MARGINS glass{0,0,CaptionHeight(),0};DwmExtendFrameIntoClientArea(m_hwnd,&glass);

    m_windowedStyle = style;
    GetWindowRect(m_hwnd, &m_windowedRect);

    return true;
}

void Window::Show(int nCmdShow)
{
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    UpdatePlaybackPower();
}

void Window::SetPreventSleep(bool enabled)
{
    m_preventSleep = enabled;
    UpdatePlaybackPower();
}

void Window::SetVideoAvailable(bool available)
{
    m_videoAvailable = available;
    UpdatePlaybackPower();
}

void Window::UpdatePlaybackPower()
{
    m_playbackPower.SetActive(m_preventSleep && m_videoAvailable && m_hwnd &&
                             IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd));
}

void Window::SetTitle(const std::wstring& title)
{
    SetWindowTextW(m_hwnd, title.c_str());
    RefreshCaption();
}

int Window::CaptionHeight() const
{
    if(m_isFullscreen||m_isPiP)return 0;
    return MulDiv(36,static_cast<int>(m_hwnd?GetDpiForWindow(m_hwnd):GetDpiForSystem()),96);
}

RECT Window::GetContentBounds() const
{
    RECT bounds{};GetClientRect(m_hwnd,&bounds);
    bounds.top=std::min(bounds.bottom,static_cast<LONG>(CaptionHeight()));
    return bounds;
}

RECT Window::CaptionButtonRect(int index) const
{
    RECT client{};GetClientRect(m_hwnd,&client);
    const int dpi=static_cast<int>(GetDpiForWindow(m_hwnd));
    const int unit=MulDiv(42,dpi,96),gap=MulDiv(8,dpi,96);
    const int left=client.right-5*unit-gap+index*unit+(index>=2?gap:0);
    return {left,0,left+unit,CaptionHeight()};
}

int Window::CaptionButtonAt(POINT point) const
{
    if(!CaptionHeight())return -1;
    for(int i=0;i<5;++i){const auto rect=CaptionButtonRect(i);if(PtInRect(&rect,point))return i;}
    return -1;
}

void Window::RefreshCaption()
{
    if(!m_hwnd)return;
    RECT rect{};GetClientRect(m_hwnd,&rect);rect.bottom=CaptionHeight();
    if(rect.bottom)InvalidateRect(m_hwnd,&rect,FALSE);
}

void Window::UpdateCaptionTooltips()
{
    m_captionTooltips={m_chromeLabels.hud[m_hudMode]+(m_hudVisible?L"":L" ("+m_chromeLabels.hidden+L")"),m_chromeLabels.settings,
        m_chromeLabels.minimize,IsZoomed(m_hwnd)?m_chromeLabels.restore:m_chromeLabels.maximize,m_chromeLabels.close};
    if(!m_captionTooltip)return;
    for(int i=0;i<5;++i){TOOLINFOW tool{sizeof(tool)};tool.hwnd=m_hwnd;tool.uId=i+1;
        tool.lpszText=m_captionTooltips[i].data();tool.rect=CaptionButtonRect(i);
        SendMessageW(m_captionTooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));
        SendMessageW(m_captionTooltip,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&tool));}
}

void Window::SetChromeState(unsigned hudMode,bool hudVisible,bool settingsVisible)
{
    if(m_hudMode==hudMode&&m_hudVisible==hudVisible&&m_settingsVisible==settingsVisible)return;
    m_hudMode=hudMode;m_hudVisible=hudVisible;m_settingsVisible=settingsVisible;
    UpdateCaptionTooltips();RefreshCaption();
}

void Window::SetChromeLabels(ChromeLabels labels)
{
    if(labels==m_chromeLabels)return;
    m_chromeLabels=std::move(labels);UpdateCaptionTooltips();
}

void Window::LayoutContent()
{
    const auto bounds=GetContentBounds();
    if(m_videoWindow)SetWindowPos(m_videoWindow,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
        SWP_NOZORDER|SWP_NOACTIVATE);
    MARGINS glass{0,0,CaptionHeight(),0};DwmExtendFrameIntoClientArea(m_hwnd,&glass);
    UpdateCaptionTooltips();RefreshCaption();
}

void Window::InvokeCaptionButton(int index)
{
    if(index==0){if(m_inputCb.onCycleHud)m_inputCb.onCycleHud();}
    else if(index==1){if(m_inputCb.onSettings)m_inputCb.onSettings();}
    else if(index==2)ShowWindow(m_hwnd,SW_MINIMIZE);
    else if(index==3)ShowWindow(m_hwnd,IsZoomed(m_hwnd)?SW_RESTORE:SW_MAXIMIZE);
    else if(index==4)PostMessageW(m_hwnd,WM_CLOSE,0,0);
}

void Window::PaintCaption(HDC dc)
{
    using namespace Gdiplus;
    RECT client{};GetClientRect(m_hwnd,&client);const int width=client.right,height=CaptionHeight();
    if(width<=0||height<=0)return;
    const float scale=GetDpiForWindow(m_hwnd)/96.f;
    Bitmap bitmap(width,height,PixelFormat32bppPARGB);Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.Clear(m_desktopGlass?Color(26,18,18,22):Color(255,30,30,34));
    const BYTE alpha=GetForegroundWindow()==m_hwnd?240:180;
    SolidBrush ink(Color(alpha,245,245,248));Pen line(Color(alpha,235,235,240),1.25f*scale);
    HICON icon=reinterpret_cast<HICON>(SendMessageW(m_hwnd,WM_GETICON,ICON_SMALL,0));
    if(!icon)icon=LoadIconW(m_hInstance,MAKEINTRESOURCEW(101));
    if(icon){Bitmap brand(icon);graphics.DrawImage(&brand,12*scale,(height-18*scale)/2,18*scale,18*scale);}
    wchar_t title[1024]{};GetWindowTextW(m_hwnd,title,1024);
    Font font(L"Segoe UI",12*scale,FontStyleRegular,UnitPixel);StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap);format.SetTrimming(StringTrimmingEllipsisCharacter);format.SetLineAlignment(StringAlignmentCenter);
    const auto first=CaptionButtonRect(0);
    RectF titleRect(39*scale,0,std::max(0.f,first.left-49*scale),static_cast<float>(height));
    graphics.DrawString(title,-1,&font,titleRect,&format,&ink);
    for(int i=0;i<5;++i){
        const auto rect=CaptionButtonRect(i);const float cx=(rect.left+rect.right)/2.f,cy=height/2.f;
        if(m_captionHover==i||m_captionPressed==i||(i==1&&m_settingsVisible)){
            SolidBrush hover(i==4?Color(220,196,43,45):i==1&&m_settingsVisible?Color(65,100,170,230):Color(m_captionPressed==i?55:30,255,255,255));
            graphics.FillRectangle(&hover,Rect(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top));
        }
        if(i==0){
            Pen hud(Color(m_hudVisible?alpha:105,235,235,240),1.3f*scale);
            if(m_hudMode==0){graphics.DrawRectangle(&hud,cx-8*scale,cy-6*scale,16*scale,12*scale);graphics.DrawLine(&hud,cx-5*scale,cy-2*scale,cx+5*scale,cy-2*scale);graphics.DrawLine(&hud,cx-5*scale,cy+2*scale,cx+2*scale,cy+2*scale);}
            else if(m_hudMode==1){for(int row=0;row<3;++row){const float y=cy+(row-1)*4*scale;graphics.DrawLine(&hud,cx-8*scale,y,cx-5*scale,y);graphics.DrawLine(&hud,cx-2*scale,y,cx+8*scale,y);}}
            else {graphics.DrawLine(&hud,cx-8*scale,cy,cx-4*scale,cy);graphics.DrawLine(&hud,cx-1*scale,cy,cx+3*scale,cy);graphics.DrawLine(&hud,cx+6*scale,cy,cx+9*scale,cy);}
        }else if(i==1){Font gear(L"Segoe MDL2 Assets",15*scale,FontStyleRegular,UnitPixel);StringFormat centered;centered.SetAlignment(StringAlignmentCenter);centered.SetLineAlignment(StringAlignmentCenter);
            graphics.DrawString(L"\uE713",1,&gear,RectF(static_cast<float>(rect.left),0,static_cast<float>(rect.right-rect.left),static_cast<float>(height)),&centered,&ink);
        }else if(i==2)graphics.DrawLine(&line,cx-5*scale,cy,cx+5*scale,cy);
        else if(i==3){if(IsZoomed(m_hwnd)){graphics.DrawRectangle(&line,cx-3*scale,cy-5*scale,8*scale,8*scale);graphics.DrawRectangle(&line,cx-5*scale,cy-3*scale,8*scale,8*scale);}else graphics.DrawRectangle(&line,cx-5*scale,cy-5*scale,10*scale,10*scale);}
        else {graphics.DrawLine(&line,cx-4.5f*scale,cy-4.5f*scale,cx+4.5f*scale,cy+4.5f*scale);graphics.DrawLine(&line,cx+4.5f*scale,cy-4.5f*scale,cx-4.5f*scale,cy+4.5f*scale);}
    }
    Pen border(Color(28,255,255,255));graphics.DrawLine(&border,0,height-1,width,height-1);graphics.Flush();
    BitmapData data;Rect area(0,0,width,height);
    if(bitmap.LockBits(&area,ImageLockModeRead,PixelFormat32bppPARGB,&data)==Ok){
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        SetDIBitsToDevice(dc,0,0,width,height,0,0,0,height,data.Scan0,&info,DIB_RGB_COLORS);bitmap.UnlockBits(&data);
    }
}

void Window::SetIcon(const std::wstring& iconPath)
{
    HICON hIcon = (HICON)LoadImageW(
        nullptr, iconPath.c_str(), IMAGE_ICON,
        0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE
    );
    
    if (hIcon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        RefreshCaption();
    }
}

void Window::SetFullscreen(bool fullscreen)
{
    if (fullscreen == m_isFullscreen) return;
    m_isFullscreen = fullscreen;

    if (fullscreen) {
        // Save current windowed position
        m_windowedStyle = GetWindowLongW(m_hwnd, GWL_STYLE);
        GetWindowRect(m_hwnd, &m_windowedRect);

        // Remove window chrome entirely
        SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

        // Win11: opt out of rounded corners and shadow when fullscreen.
        // DWMWA_WINDOW_CORNER_PREFERENCE was added in build 22000.
        // 1 = DONOTROUND.
        DWORD cornerPref = 1; // DWMWCP_DONOTROUND
        DwmSetWindowAttribute(m_hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                              &cornerPref, sizeof(cornerPref));

        HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);

        // Use rcMonitor (full monitor including taskbar area) for borderless
        // fullscreen, covering the screen edge-to-edge.
        SetWindowPos(m_hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        // Restore rounded corners (Win11 default)
        DWORD cornerPref = 0; // DWMWCP_DEFAULT
        DwmSetWindowAttribute(m_hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                              &cornerPref, sizeof(cornerPref));

        SetWindowLongW(m_hwnd, GWL_STYLE, m_windowedStyle);
        SetWindowPos(m_hwnd, HWND_NOTOPMOST,
            m_windowedRect.left, m_windowedRect.top,
            m_windowedRect.right - m_windowedRect.left,
            m_windowedRect.bottom - m_windowedRect.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }

    m_wasResized = true;
    LayoutContent();
}

void Window::SetPiP(bool enabled, uint32_t width, uint32_t height, float opacity)
{
    if (enabled == m_isPiP) return; // No-op if already in target state
    EndPiPResize();
    m_pipScaleAspect = 0.0;
    m_isPiP = enabled;

    // Ensure there is a valid windowed rect to restore to
    if (m_windowedRect.right - m_windowedRect.left < 100) {
        // First-time toggle or invalid state -- pick sane defaults
        m_windowedRect.left   = 100;
        m_windowedRect.top    = 100;
        m_windowedRect.right  = 100 + 1280;
        m_windowedRect.bottom = 100 + 720;
    }

    if (enabled) {
        // Save current position before transitioning
        if (!m_isFullscreen) {
            GetWindowRect(m_hwnd, &m_windowedRect);
            m_windowedStyle = GetWindowLongW(m_hwnd, GWL_STYLE);
        }

        m_pipWidth = width;
        m_pipHeight = height;

        // Get monitor work area to position PiP in bottom-right corner
        HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);

        // Preferred-position path: honor the application's saved (or
        // hotkey-updated) PiP top-left when it's been set. Clamp it inside
        // the current monitor's work area so a coord saved from a different
        // monitor layout still lands somewhere visible. -1 / -1 sentinel
        // means "use the default bottom-right corner".
        int pipX, pipY;
        if (m_pipPreferredX != -1 || m_pipPreferredY != -1) {
            pipX = m_pipPreferredX;
            pipY = m_pipPreferredY;
            const int maxX = mi.rcWork.right  - (int)width;
            const int maxY = mi.rcWork.bottom - (int)height;
            if (pipX < mi.rcWork.left) pipX = mi.rcWork.left;
            if (pipY < mi.rcWork.top)  pipY = mi.rcWork.top;
            if (pipX > maxX)           pipX = maxX;
            if (pipY > maxY)           pipY = maxY;
        } else {
            pipX = mi.rcWork.right  - (int)width  - 20;
            pipY = mi.rcWork.bottom - (int)height - 20;
        }

        // Remove chrome, set always-on-top
        SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongW(m_hwnd, GWL_EXSTYLE,
            GetWindowLongW(m_hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOPMOST);
        SetLayeredWindowAttributes(m_hwnd, 0, (BYTE)(opacity * 255), LWA_ALPHA);

        SetWindowPos(m_hwnd, HWND_TOPMOST,
            pipX, pipY, (int)width, (int)height,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        // Restore normal window.
        //
        // Order matters here. The prior code removed WS_EX_LAYERED first
        // and never reset SetLayeredWindowAttributes' alpha: DWM's
        // layered-window cache held onto the old per-window alpha, and
        // any subsequent code path that re-applied WS_EX_LAYERED (or that
        // the OS re-applied implicitly during a fast Alt+Enter from PiP)
        // could pick up the stale 230-ish alpha and leave the main
        // window visibly transparent. The flow now:
        //
        //   1. Force the alpha back to 255 while WS_EX_LAYERED is still
        //      set: guaranteed to land a fully opaque state in DWM's
        //      cache regardless of what comes next.
        //   2. Strip WS_EX_LAYERED and WS_EX_TOPMOST.
        //   3. Restore the saved window style (chrome / titlebar).
        //   4. Re-show as a non-topmost window with SWP_FRAMECHANGED so
        //      DWM picks up the new style flags immediately.
        //   5. Issue a no-move SetWindowPos with SWP_FRAMECHANGED again
        //      after the style swap as belt-and-suspenders: some Windows
        //      builds need a second frame-change to fully drop layered
        //      compositing.
        SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
        SetWindowLongW(m_hwnd, GWL_EXSTYLE,
            GetWindowLongW(m_hwnd, GWL_EXSTYLE) & ~(WS_EX_LAYERED | WS_EX_TOPMOST));
        SetWindowLongW(m_hwnd, GWL_STYLE, m_windowedStyle);

        SetWindowPos(m_hwnd, HWND_NOTOPMOST,
            m_windowedRect.left, m_windowedRect.top,
            m_windowedRect.right - m_windowedRect.left,
            m_windowedRect.bottom - m_windowedRect.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    LayoutContent();
    m_wasResized = true;
}

bool Window::SetPiPOpacity(float opacity)
{
    if (!m_isPiP || !m_hwnd || !std::isfinite(opacity)) return false;
    // A visible minimum keeps the window reachable with the mouse.
    const BYTE alpha = static_cast<BYTE>(std::lround(std::clamp(opacity, 0.1f, 1.0f) * 255));
    return SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA) != FALSE;
}

bool Window::NudgePiP(int dx, int dy)
{
    if (!m_isPiP || !m_hwnd || m_pipResizeEdges) return false;

    RECT rc{};
    if (!GetWindowRect(m_hwnd, &rc)) return false;
    const int curW = rc.right  - rc.left;
    const int curH = rc.bottom - rc.top;
    int newX = rc.left + dx;
    int newY = rc.top  + dy;

    // Clamp to the work area of whichever monitor currently contains the
    // PiP window. Lets the user nudge from one corner to another but not
    // off-screen.
    HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi)) {
        const int maxX = mi.rcWork.right  - curW;
        const int maxY = mi.rcWork.bottom - curH;
        if (newX < mi.rcWork.left) newX = mi.rcWork.left;
        if (newY < mi.rcWork.top)  newY = mi.rcWork.top;
        if (newX > maxX)           newX = maxX;
        if (newY > maxY)           newY = maxY;
    }

    if (newX == rc.left && newY == rc.top) return false;

    SetWindowPos(m_hwnd, HWND_TOPMOST, newX, newY, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);

    // Cache the new preferred so any subsequent SetPiP(true) re-applies it
    // (e.g. if the user toggles fullscreen and back, or restarts the app).
    m_pipPreferredX = newX;
    m_pipPreferredY = newY;
    return true;
}

bool Window::GetPiPWorkArea(RECT& work) const
{
    HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return false;
    work = mi.rcWork;
    return work.right - work.left >= 80 && work.bottom - work.top >= 45;
}

bool Window::ResizePiP(int dw, int dh, bool keepAspect)
{
    if (!m_isPiP || !m_hwnd || m_pipResizeEdges) return false;

    RECT rc{}, work{};
    if (!GetWindowRect(m_hwnd, &rc) || !GetPiPWorkArea(work)) return false;
    const int maxWidth = std::min(16384L, work.right - work.left);
    const int maxHeight = std::min(16384L, work.bottom - work.top);

    // Signed arithmetic lets shrinking reach the minimum without wrapping.
    int width = static_cast<int>(std::clamp<int64_t>(
        int64_t(rc.right - rc.left) + dw, 80, maxWidth));
    int height = static_cast<int>(std::clamp<int64_t>(
        int64_t(rc.bottom - rc.top) + dh, 45, maxHeight));
    if (keepAspect) {
        if (rc.right <= rc.left || rc.bottom <= rc.top) return false;
        // Retaining the ratio across repeats prevents pixel rounding drift.
        if (m_pipScaleAspect == 0.0)
            m_pipScaleAspect = double(rc.right - rc.left) / (rc.bottom - rc.top);
        const auto size = ScalePiPSize(double(rc.right - rc.left) + dw,
                                      m_pipScaleAspect, maxWidth, maxHeight);
        width = size.first;
        height = size.second;
        if (width == 0 || height == 0) return false;
    }
    const int x = std::clamp(rc.left, work.left, work.right - width);
    const int y = std::clamp(rc.top, work.top, work.bottom - height);
    if (!ApplyPiPRect({x, y, x + width, y + height})) return false;
    if (!keepAspect) m_pipScaleAspect = 0.0;
    return true;
}

bool Window::ApplyPiPRect(const RECT& rect)
{
    RECT current{};
    if (!GetWindowRect(m_hwnd, &current) || EqualRect(&current, &rect)) return false;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (!SetWindowPos(m_hwnd, HWND_TOPMOST, rect.left, rect.top, width, height,
                      SWP_NOACTIVATE)) return false;

    m_pipWidth = width;
    m_pipHeight = height;
    m_pipPreferredX = rect.left;
    m_pipPreferredY = rect.top;
    m_wasResized = true;
    if (m_onPiPResize) m_onPiPResize();
    return true;
}

int Window::HitTestPiPResize(POINT screenPoint) const
{
    if (!m_isPiP || !m_hwnd) return 0;
    RECT rc{};
    if (!GetWindowRect(m_hwnd, &rc) || !PtInRect(&rc, screenPoint)) return 0;
    const int grip = std::min(MulDiv(8, GetDpiForWindow(m_hwnd), 96),
                             int(std::min(rc.right - rc.left, rc.bottom - rc.top) / 3));
    int edges = 0;
    if (screenPoint.x < rc.left + grip) edges |= kResizeLeft;
    if (screenPoint.x >= rc.right - grip) edges |= kResizeRight;
    if (screenPoint.y < rc.top + grip) edges |= kResizeTop;
    if (screenPoint.y >= rc.bottom - grip) edges |= kResizeBottom;
    return edges;
}

bool Window::BeginPiPResize(POINT screenPoint)
{
    const int edges = HitTestPiPResize(screenPoint);
    if (!edges || !GetWindowRect(m_hwnd, &m_pipResizeStart) ||
        !GetPiPWorkArea(m_pipResizeWork)) return false;
    m_pipResizeMouse = screenPoint;
    m_pipResizeEdges = edges;
    m_pipScaleAspect = 0.0;
    // Mouse capture keeps playback on the normal message loop during dragging.
    SetCapture(m_hwnd);
    if (GetCapture() != m_hwnd) m_pipResizeEdges = 0;
    return m_pipResizeEdges != 0;
}

void Window::UpdatePiPResize(POINT screenPoint)
{
    if (!m_isPiP || !m_pipResizeEdges) return;
    const bool left = (m_pipResizeEdges & kResizeLeft) != 0;
    const bool top = (m_pipResizeEdges & kResizeTop) != 0;
    const bool horizontal = (m_pipResizeEdges & (kResizeLeft | kResizeRight)) != 0;
    const bool vertical = (m_pipResizeEdges & (kResizeTop | kResizeBottom)) != 0;
    const RECT& start = m_pipResizeStart;
    const int startWidth = start.right - start.left;
    const int startHeight = start.bottom - start.top;
    const int maxWidth = std::min(16384L, left ? start.right - m_pipResizeWork.left
                                               : m_pipResizeWork.right - start.left);
    const int maxHeight = std::min(16384L, top ? start.bottom - m_pipResizeWork.top
                                              : m_pipResizeWork.bottom - start.top);
    if (startWidth <= 0 || startHeight <= 0 || maxWidth < 80 || maxHeight < 45) return;
    const int64_t dx = int64_t(screenPoint.x) - m_pipResizeMouse.x;
    const int64_t dy = int64_t(screenPoint.y) - m_pipResizeMouse.y;
    const int64_t requestedWidth = startWidth + (horizontal ? (left ? -dx : dx) : 0);
    const int64_t requestedHeight = startHeight + (vertical ? (top ? -dy : dy) : 0);
    int width = int(std::clamp<int64_t>(requestedWidth, 80, maxWidth));
    int height = int(std::clamp<int64_t>(requestedHeight, 45, maxHeight));
    if (horizontal && vertical) {
        const double aspect = double(startWidth) / startHeight;
        const double scaledWidth = std::abs(double(dx) / startWidth) >= std::abs(double(dy) / startHeight)
            ? double(requestedWidth) : requestedHeight * aspect;
        const auto size = ScalePiPSize(scaledWidth, aspect, maxWidth, maxHeight);
        width = size.first;
        height = size.second;
        if (width == 0 || height == 0) return;
    }
    const int x = left ? start.right - width : start.left;
    const int y = top ? start.bottom - height : start.top;
    ApplyPiPRect({x, y, x + width, y + height});
}

void Window::EndPiPResize()
{
    if (!m_pipResizeEdges) return;
    m_pipResizeEdges = 0;
    if (GetCapture() == m_hwnd) ReleaseCapture();
}

bool Window::GetPiPPosition(int32_t& outX, int32_t& outY) const
{
    if (!m_isPiP || !m_hwnd) return false;
    RECT rc{};
    if (!GetWindowRect(m_hwnd, &rc)) return false;
    outX = rc.left;
    outY = rc.top;
    return true;
}

std::pair<uint32_t, uint32_t> Window::GetClientSize() const
{
    RECT rc{}; // zero-init so stack garbage is never returned if GetClientRect fails
    if (!m_hwnd || !GetClientRect(m_videoWindow?m_videoWindow:m_hwnd, &rc)) {
        return { 0, 0 };
    }
    LONG w = rc.right  - rc.left;
    LONG h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return { 0, 0 };
    return { (uint32_t)w, (uint32_t)h };
}

LRESULT CALLBACK Window::VideoWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self){
        if(msg==WM_MOUSEMOVE||msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP){
            POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};
            MapWindowPoints(hwnd,self->m_hwnd,&point,1);
            if(msg==WM_LBUTTONDOWN)SetFocus(self->m_hwnd);
            return SendMessageW(self->m_hwnd,msg,wParam,MAKELPARAM(point.x,point.y));
        }
        if(msg==WM_MOUSEWHEEL||msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN||msg==WM_SETCURSOR)
            return SendMessageW(self->m_hwnd,msg,wParam,lParam);
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        self->m_hwnd=hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_NCCALCSIZE:
        if(self&&!self->m_isFullscreen&&!self->m_isPiP&&wParam){
            auto* parameters=reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);const LONG top=parameters->rgrc[0].top;
            DefWindowProcW(hwnd,msg,wParam,lParam);
            const UINT dpi=GetDpiForWindow(hwnd);
            parameters->rgrc[0].top=top+(IsZoomed(hwnd)?GetSystemMetricsForDpi(SM_CYSIZEFRAME,dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi):0);
            return 0;
        }
        break;
    case WM_NCHITTEST:
        if(self&&self->CaptionHeight()){
            POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};ScreenToClient(hwnd,&point);
            if(self->CaptionButtonAt(point)>=0)return HTCLIENT;
            RECT client{};GetClientRect(hwnd,&client);const UINT dpi=GetDpiForWindow(hwnd);
            const int edge=GetSystemMetricsForDpi(SM_CXSIZEFRAME,dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi);
            if(!IsZoomed(hwnd)&&point.y<edge){if(point.x<edge)return HTTOPLEFT;if(point.x>=client.right-edge)return HTTOPRIGHT;return HTTOP;}
            if(point.y<self->CaptionHeight()&&point.x>=0&&point.x<client.right)return point.x<MulDiv(34,dpi,96)?HTSYSMENU:HTCAPTION;
        }
        break;
    case WM_PAINT:
        if(self){PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);self->PaintCaption(dc);EndPaint(hwnd,&paint);return 0;}
        break;
    case WM_DPICHANGED:
        if(self){const auto* rect=reinterpret_cast<RECT*>(lParam);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOACTIVATE|SWP_NOZORDER);self->LayoutContent();return 0;}
        break;
    case WM_MOUSELEAVE:
        if(self&&self->m_captionHover!=-1){self->m_captionHover=-1;self->RefreshCaption();}
        return 0;
    case WM_SETCURSOR:
        if (self && (LOWORD(lParam) == HTCLIENT || self->m_pipResizeEdges)) {
            POINT point{};
            if (!GetCursorPos(&point)) break;
            const int edges = self->m_pipResizeEdges ? self->m_pipResizeEdges
                                                   : self->HitTestPiPResize(point);
            if (edges) {
                LPCWSTR cursor = IDC_SIZEWE;
                if (edges == (kResizeLeft | kResizeTop) || edges == (kResizeRight | kResizeBottom))
                    cursor = IDC_SIZENWSE;
                else if (edges == (kResizeRight | kResizeTop) || edges == (kResizeLeft | kResizeBottom))
                    cursor = IDC_SIZENESW;
                else if (edges == kResizeTop || edges == kResizeBottom)
                    cursor = IDC_SIZENS;
                SetCursor(LoadCursorW(nullptr, cursor));
                return TRUE;
            }
        }
        break;

    case WM_CAPTURECHANGED:
        if (self) {self->m_pipResizeEdges = 0;self->m_captionPressed=-1;self->RefreshCaption();}
        break;

    case WM_CANCELMODE:
    case WM_KILLFOCUS:
        if (self) self->EndPiPResize();
        break;

    case WM_SIZE:
        if (self) {
            if (wParam == SIZE_MINIMIZED) self->m_playbackPower.SetActive(false);
            else self->UpdatePlaybackPower();
        }
        // Set the resize flag for any size change EXCEPT minimize (which
        // gives 0x0 dimensions and would crash the renderer)
        if (self && wParam != SIZE_MINIMIZED) {
            self->LayoutContent();
            self->m_wasResized = true;
            // Only the normal window size belongs to the next windowed launch.
            if (wParam == SIZE_RESTORED && !self->m_isFullscreen && !self->m_isPiP) {
                const auto bounds=self->GetContentBounds();
                const uint32_t width = bounds.right-bounds.left, height = bounds.bottom-bounds.top;
                if (width > 0 && height > 0) self->m_windowedClientSize = {width, height};
            }
            // SIZE_MAXIMIZED and SIZE_RESTORED are both valid -- both come
            // through this path and trigger a clean resize on the next frame.
        }
        return 0;

    case WM_DISPLAYCHANGE:
        // Defer DisplayConfig queries until after message dispatch; no
        // topology query or renderer rebuild runs in the window procedure.
        if (self) self->m_displayChanged = true;
        return 0;

    case WM_SHOWWINDOW:
        if (self) {
            if (!wParam) self->m_playbackPower.SetActive(false);
            else self->UpdatePlaybackPower();
        }
        break;

    case WM_SYSCOMMAND:
        if (self && self->m_playbackPower.SuppressesScreenSaver(wParam)) return 0;
        break;

    case WM_MOVE:
        // The main window moved on screen. The renderer doesn't care about
        // window position, but the WebView2 settings popup is a separate
        // top-level window pinned to main's client rect: when main moves,
        // the popup must follow or it ends up stranded at its old screen
        // position, appearing to "disappear" because it's no longer over
        // main's client area.
        //
        // Fire the synchronous callback HERE inside WndProc, not via the
        // m_wasMoved flag. During a title-bar drag, Windows runs a modal
        // message loop and the app's PeekMessage pump is suspended: the
        // flag wouldn't be consumed until the drag finished, making the
        // popup look stranded mid-drag. The callback fires on every WM_MOVE
        // including the ones delivered inside the modal loop.
        if (self) {
            self->m_wasMoved = true;
            if (self->m_onMove) self->m_onMove();
        }
        return 0;

    case WM_ENTERSIZEMOVE:
        // User started dragging the window edge. WM_SIZE won't fire again
        // until they release. No-op for now but kept for symmetry.
        return 0;

    case WM_EXITSIZEMOVE:
        // User finished dragging. Two things to do:
        //   1. Flag a resize so the renderer + overlay catch up to the
        //      final dimensions (in case WM_SIZE didn't fire for a
        //      same-size drag, or fired multiple times during drag).
        //   2. Fire the move callback once more. During the drag the
        //      popup was tracking via WM_MOVE, but on drag release
        //      Windows reactivates the main window and may shuffle
        //      z-order, leaving the popup hidden behind main. The
        //      callback's TrackOwnerRect call re-asserts the popup
        //      above its owner via SetWindowPos(popup, owner).
        if (self) {
            self->m_wasResized = true;
            if (self->m_onMove) self->m_onMove();
        }
        return 0;

    case WM_ACTIVATE:
        if(self)self->RefreshCaption();
        // Main window gained or lost activation. If GAINED (e.g. user
        // clicked away then back, or finished a title-bar drag), the
        // popup may have ended up underneath in z-order. Re-assert by
        // calling the same TrackOwnerRect path. LOWORD(wParam) ==
        // WA_INACTIVE means focus was just LOST: ignore that case;
        // only re-stack on a GAIN-focus transition.
        if (self && self->m_onMove && LOWORD(wParam) != WA_INACTIVE) {
            self->m_onMove();
        }
        break;  // let DefWindowProc handle the rest of WM_ACTIVATE

    case WM_GETMINMAXINFO:
        // Allow window to be resized down to small but not zero -- prevents
        // swap chain trying to allocate a 0-byte buffer.
        if (lParam) {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = 320;
            mmi->ptMinTrackSize.y = 240;
        }
        return 0;

    case WM_KEYDOWN:
        // Alt+Enter is intentionally NOT handled here: HotkeyManager owns
        // it (registered in Application::Initialize as "toggle_fullscreen").
        // Listening here too caused a double-toggle race where both paths
        // fired on the same keypress and cancelled out, so users had to
        // press Alt+Enter twice to see a state change.
        //
        // Forward to UI callbacks (Esc, arrows, etc.)
        if (self && self->m_inputCb.onKeyDown) {
            self->m_inputCb.onKeyDown((int)wParam);
        }
        break;

    case WM_MOUSEMOVE:
        if(self){const int hover=self->CaptionButtonAt({static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))});
            if(hover!=self->m_captionHover){self->m_captionHover=hover;self->RefreshCaption();}
            if(hover>=0){TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);return 0;}}
        if (self && self->m_pipResizeEdges) {
            POINT point{(short)LOWORD(lParam), (short)HIWORD(lParam)};
            if (ClientToScreen(hwnd, &point)) self->UpdatePiPResize(point);
            return 0;
        }
        if (self && self->m_inputCb.onMouseMove) {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam)-self->CaptionHeight();
            self->m_inputCb.onMouseMove(x, y);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if(self){const int button=self->CaptionButtonAt({static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))});
            if(button>=0){self->m_captionPressed=button;SetCapture(hwnd);self->RefreshCaption();return 0;}}
        if (self && self->m_isPiP) {
            POINT point{(short)LOWORD(lParam), (short)HIWORD(lParam)};
            if (ClientToScreen(hwnd, &point) && self->BeginPiPResize(point)) return 0;
        }
        if (self && self->m_inputCb.onMouseDown) {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam)-self->CaptionHeight();
            // Capture the mouse so drags keep working when cursor leaves the window
            SetCapture(hwnd);
            self->m_inputCb.onMouseDown(x, y);
        }
        return 0;

    case WM_LBUTTONUP:
        if(self&&self->m_captionPressed>=0){const int button=self->m_captionPressed;self->m_captionPressed=-1;ReleaseCapture();self->RefreshCaption();
            if(self->CaptionButtonAt({static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))})==button)self->InvokeCaptionButton(button);return 0;}
        if (self && self->m_pipResizeEdges) {
            POINT point{(short)LOWORD(lParam), (short)HIWORD(lParam)};
            if (ClientToScreen(hwnd, &point)) self->UpdatePiPResize(point);
            self->EndPiPResize();
            return 0;
        }
        if (self && self->m_inputCb.onMouseUp) {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam)-self->CaptionHeight();
            ReleaseCapture();
            self->m_inputCb.onMouseUp(x, y);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (self && self->m_inputCb.onMouseWheel) {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            self->m_inputCb.onMouseWheel(delta);
        }
        return 0;

    case WM_SYSKEYDOWN:
        // Prevent Alt from activating menu
        if (wParam == VK_MENU) return 0;
        // Alt+Enter is owned by HotkeyManager (see WM_KEYDOWN comment above).
        // Returning here without handling lets DefWindowProc do its thing,
        // which is harmless for Alt+Enter since there is no system menu.
        break;

    case WM_CLOSE:
        // Pending settings are committed while the child browser is still alive.
        if (self && self->m_onClose) self->m_onClose();
        break;

    case WM_DESTROY:
        if (self) self->SetVideoAvailable(false);
        PostQuitMessage(0);
        return 0;

    case WM_ERASEBKGND:
        return 1; // Prevent flicker
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace NitLink
