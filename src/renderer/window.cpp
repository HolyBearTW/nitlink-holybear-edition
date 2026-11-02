#include "window.h"
#include "window_composition.h"
#include "window_motion.h"
#include "../resource.h"
#include "../app/holybear_version.h"
#include <algorithm>
#include <cmath>
#include <dwmapi.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace NitLink {

namespace {
constexpr int kResizeLeft = 1, kResizeRight = 2, kResizeTop = 4, kResizeBottom = 8;
constexpr UINT_PTR kWindowMotionTimer = 0x4e4c4d4f;

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
    StopMotion();
    SetVideoAvailable(false);
    m_composition.reset();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_captionLogo.reset();
    m_captionFonts.reset();
    if(m_gdiplusToken)Gdiplus::GdiplusShutdown(m_gdiplusToken);
}

bool Window::Create(HINSTANCE hInstance, const Desc& desc)
{
    m_hInstance = hInstance;
    m_isSettingsWindow=desc.settingsWindow;
    m_windowedClientSize = {desc.width, desc.height};
    Gdiplus::GdiplusStartupInput graphicsStartup;
    if(Gdiplus::GdiplusStartup(&m_gdiplusToken,&graphicsStartup,nullptr)!=Gdiplus::Ok)return false;
    if(const auto resource=FindResourceW(hInstance,MAKEINTRESOURCEW(IDR_CAPTION_LOCKUP),RT_RCDATA)){
        const auto bytes=LoadResource(hInstance,resource);
        if(auto* stream=SHCreateMemStream(static_cast<const BYTE*>(LockResource(bytes)),SizeofResource(hInstance,resource))){
            std::unique_ptr<Gdiplus::Bitmap> source(Gdiplus::Bitmap::FromStream(stream));
            if(source&&source->GetLastStatus()==Gdiplus::Ok)
                m_captionLogo.reset(source->Clone(0,0,static_cast<INT>(source->GetWidth()),static_cast<INT>(source->GetHeight()),PixelFormat32bppPARGB));
            source.reset();stream->Release();
        }
    }
    if(const auto resource=FindResourceW(hInstance,MAKEINTRESOURCEW(IDR_CAPTION_ARCHIVO),RT_RCDATA)){
        const auto bytes=LoadResource(hInstance,resource);
        m_captionFonts=std::make_unique<Gdiplus::PrivateFontCollection>();
        m_captionFonts->AddMemoryFont(LockResource(bytes),SizeofResource(hInstance,resource));
    }

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
    // If the resource is missing for any reason, fall back gracefully to
    // the generic IDI_APPLICATION icon so the app still launches.
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm       = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON),
                                            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!wc.hIconSm) wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    
    WNDCLASSEXW registeredWindow{sizeof(registeredWindow)};
    if (!GetClassInfoExW(hInstance,wc.lpszClassName,&registeredWindow)&&!RegisterClassExW(&wc)) {
        return false;
    }

    // Calculate window size for desired client area
    RECT rc = {0, 0, (LONG)desc.width, (LONG)desc.height};
    // Only NitLink paints the caption. In particular, omit WS_SYSMENU as
    // well as WS_CAPTION so DWM cannot paint a second set of caption items.
    // Minimize/maximize still use the standard window system commands.
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
    const UINT dpi=GetDpiForSystem();
    rc.bottom+=MulDiv(36,static_cast<int>(dpi),96);

    m_hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW,
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
    WNDCLASSEXW registeredVideo{sizeof(registeredVideo)};
    if(!GetClassInfoExW(hInstance,videoClass.lpszClassName,&registeredVideo)&&!RegisterClassExW(&videoClass))return false;
    const auto content=GetContentBounds();
    m_videoWindow=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,videoClass.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
        content.left,content.top,content.right-content.left,content.bottom-content.top,m_hwnd,nullptr,hInstance,this);
    if(!m_videoWindow)return false;

    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    m_captionTooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,m_hwnd,nullptr,hInstance,nullptr);
    for(int i=m_isSettingsWindow?2:0;i<5;++i){TOOLINFOW tool{sizeof(tool)};tool.hwnd=m_hwnd;tool.uId=i+1;tool.uFlags=TTF_SUBCLASS;
        tool.rect=CaptionButtonRect(i);tool.lpszText=const_cast<wchar_t*>(L"");SendMessageW(m_captionTooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
    UpdateCaptionTooltips();

    // Enable dark title bar (Windows 11+)
    if (desc.darkMode) {
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(m_hwnd, 
            20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, 
            &darkMode, sizeof(darkMode));
    }

    m_composition=std::make_unique<WindowComposition>();
    if(!m_composition->Initialize(m_hwnd))m_composition.reset();

    // The separate glass surface owns the rounded edge and shadow. The
    // content window must stay transparent wherever it has no picture/text.
    auto cornerPref = m_composition?1:2;
    DwmSetWindowAttribute(m_hwnd, 
        33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, 
        &cornerPref, sizeof(cornerPref));
    int backdrop=m_composition?1:3; // NONE for separate glass, transient fallback.
    m_desktopGlass=SUCCEEDED(DwmSetWindowAttribute(m_hwnd,38,&backdrop,sizeof(backdrop)));
    if(m_composition){
        const DWMNCRENDERINGPOLICY policy=DWMNCRP_DISABLED;
        DwmSetWindowAttribute(m_hwnd,DWMWA_NCRENDERING_POLICY,&policy,sizeof(policy));
        const COLORREF noBorder=0xfffffffe;
        DwmSetWindowAttribute(m_hwnd,34,&noBorder,sizeof(noBorder));
        BOOL noPeek=TRUE;DwmSetWindowAttribute(m_hwnd,11,&noPeek,sizeof(noPeek));
        BOOL customTransitions=TRUE;
        DwmSetWindowAttribute(m_hwnd,DWMWA_TRANSITIONS_FORCEDISABLED,&customTransitions,sizeof(customTransitions));
    }
    // Apply the custom client frame before the first visible paint.
    SetWindowPos(m_hwnd,nullptr,0,0,0,0,
        SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutContent();

    m_windowedStyle = style;
    GetWindowRect(m_hwnd, &m_windowedRect);

    return true;
}

void Window::Show(int nCmdShow)
{
    const bool animate=ShouldAnimate()&&nCmdShow!=SW_SHOWMINIMIZED&&nCmdShow!=SW_MINIMIZE&&nCmdShow!=SW_SHOWMINNOACTIVE;
    if(animate){
        SetWindowLongW(m_hwnd,GWL_EXSTYLE,GetWindowLongW(m_hwnd,GWL_EXSTYLE)|WS_EX_LAYERED);
        SetLayeredWindowAttributes(m_hwnd,0,0,LWA_ALPHA);m_contentOpacity=0;
    }
    ShowWindow(m_hwnd, nCmdShow);
    LayoutContent();
    UpdateWindow(m_hwnd);
    if(IsWindowVisible(m_hwnd)&&!IsIconic(m_hwnd)){
        BeginFade(true);
        // The initial status picture is already presented. Complete this short
        // entrance before synchronous capture initialization starts; otherwise
        // its timer would wait behind device enumeration with an invisible UI.
        // The independent glass spring continues on the system compositor.
        while(m_fade.active){AdvanceMotion();if(m_fade.active)DwmFlush();}
    }
    UpdatePlaybackPower();
}

bool Window::ShouldAnimate() const
{
    return m_composition&&!m_isFullscreen&&!m_isPiP&&m_composition->AnimationsEnabled();
}

void Window::BeginFade(bool show,UINT completionCommand)
{
    const bool animate=ShouldAnimate();
    if(m_composition){UpdateGlassPosition();m_composition->AnimateGlass(show,animate);}
    m_fade={true,GetTickCount64(),animate?(show?WindowMotion::NormalMs:WindowMotion::FastMs):0,
        m_contentOpacity,show?1.f:0.f,completionCommand};
    if(animate){
        SetWindowLongW(m_hwnd,GWL_EXSTYLE,GetWindowLongW(m_hwnd,GWL_EXSTYLE)|WS_EX_LAYERED);
        SetLayeredWindowAttributes(m_hwnd,0,static_cast<BYTE>(std::lround(m_contentOpacity*255)),LWA_ALPHA);
    }
    SetTimer(m_hwnd,kWindowMotionTimer,16,nullptr);
    AdvanceMotion();
}

void Window::AdvanceMotion()
{
    const auto now=GetTickCount64();
    if(m_glide.active){
        const double time=static_cast<double>(now-m_glide.started)/WindowMotion::GlideMs;
        if(time>=1)EndGlide();
        else{
            const double progress=WindowMotion::Progress(WindowMotion::Glide,time);
            const auto interpolate=[&](LONG from,LONG to){return static_cast<LONG>(std::lround(from+(to-from)*progress));};
            m_glide.drawn={interpolate(m_glide.from.left,m_glide.to.left),interpolate(m_glide.from.top,m_glide.to.top),
                interpolate(m_glide.from.right,m_glide.to.right),interpolate(m_glide.from.bottom,m_glide.to.bottom)};
            const int x=m_glide.drawn.left-m_glide.host.left,y=m_glide.drawn.top-m_glide.host.top;
            const int diameter=static_cast<int>(std::lround(2*WindowMotion::Radius*GetDpiForWindow(m_hwnd)/96.f));
            auto region=CreateRoundRectRgn(x,y,x+m_glide.drawn.right-m_glide.drawn.left,
                y+m_glide.drawn.bottom-m_glide.drawn.top,diameter,diameter);
            if(!SetWindowRgn(m_hwnd,region,TRUE))DeleteObject(region);
            UpdateGlassPosition();
        }
    }
    if(m_fade.active){
        const double time=m_fade.duration?static_cast<double>(now-m_fade.started)/m_fade.duration:1;
        const double progress=WindowMotion::Progress(WindowMotion::Smooth,time);
        m_contentOpacity=static_cast<float>(m_fade.from+(m_fade.to-m_fade.from)*progress);
        SetLayeredWindowAttributes(m_hwnd,0,static_cast<BYTE>(std::lround(m_contentOpacity*255)),LWA_ALPHA);
        if(time>=1){
            const auto command=m_fade.completionCommand;m_fade.active=false;
            if(command){
                if(m_composition)m_composition->HideGlass();
                // Windows still owns taskbar/minimized state and normal close
                // handling, after both visual layers have faded out.
                DefWindowProcW(m_hwnd,WM_SYSCOMMAND,command,0);
                if(command==SC_CLOSE)return;
            }
            m_contentOpacity=1;
            SetLayeredWindowAttributes(m_hwnd,0,255,LWA_ALPHA);
            SetWindowLongW(m_hwnd,GWL_EXSTYLE,GetWindowLongW(m_hwnd,GWL_EXSTYLE)&~WS_EX_LAYERED);
        }
    }
    if(!m_fade.active&&!m_glide.active)KillTimer(m_hwnd,kWindowMotionTimer);
}

void Window::EndGlide()
{
    if(!m_glide.active)return;
    const auto target=m_glide.to;m_glide.active=false;
    m_regionSize={};
    SetWindowPos(m_hwnd,nullptr,target.left,target.top,target.right-target.left,target.bottom-target.top,
        SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    LayoutContent();m_wasResized=true;
    if(m_onMove)m_onMove();
}

void Window::StopMotion()
{
    EndGlide();m_fade.active=false;
    if(!m_hwnd)return;
    KillTimer(m_hwnd,kWindowMotionTimer);
    if(!m_isPiP){
        SetLayeredWindowAttributes(m_hwnd,0,255,LWA_ALPHA);
        SetWindowLongW(m_hwnd,GWL_EXSTYLE,GetWindowLongW(m_hwnd,GWL_EXSTYLE)&~WS_EX_LAYERED);
    }
    m_contentOpacity=1;
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

RECT Window::LayoutBounds() const
{
    RECT bounds{};GetClientRect(m_hwnd,&bounds);
    if(m_glide.active){
        // One final content layout throughout the glide. Only its visible
        // window region and the separate glass move frame by frame.
        bounds.left=m_glide.to.left-m_glide.host.left;
        bounds.top=m_glide.to.top-m_glide.host.top;
        bounds.right=bounds.left+m_glide.client.cx;
        bounds.bottom=bounds.top+m_glide.client.cy;
    }
    return bounds;
}

RECT Window::GetContentBounds() const
{
    auto bounds=LayoutBounds();
    bounds.top=std::min(bounds.bottom,bounds.top+CaptionHeight());
    return bounds;
}

RECT Window::CaptionButtonRect(int index) const
{
    const auto client=LayoutBounds();
    const int dpi=static_cast<int>(GetDpiForWindow(m_hwnd));
    const int unit=MulDiv(28,dpi,96),step=MulDiv(30,dpi,96),gap=MulDiv(10,dpi,96);
    const int left=client.right-MulDiv(10,dpi,96)-unit-(4-index)*step-(index<2?gap:0);
    const int top=client.top+(CaptionHeight()-unit)/2;
    return {left,top,left+unit,top+unit};
}

int Window::CaptionButtonAt(POINT point) const
{
    if(!CaptionHeight())return -1;
    for(int i=m_isSettingsWindow?2:0;i<5;++i){const auto rect=CaptionButtonRect(i);if(PtInRect(&rect,point))return i;}
    return -1;
}

void Window::RefreshCaption()
{
    if(!m_hwnd)return;
    auto rect=LayoutBounds();rect.bottom=rect.top+CaptionHeight();
    if(CaptionHeight())InvalidateRect(m_hwnd,&rect,FALSE);
}

void Window::UpdateCaptionTooltips()
{
    m_captionTooltips={m_chromeLabels.hud[m_hudMode]+(m_hudVisible?L"":L" ("+m_chromeLabels.hidden+L")"),m_chromeLabels.settings,
        m_chromeLabels.minimize,IsZoomed(m_hwnd)?m_chromeLabels.restore:m_chromeLabels.maximize,m_chromeLabels.close};
    if(!m_captionTooltip)return;
    for(int i=m_isSettingsWindow?2:0;i<5;++i){TOOLINFOW tool{sizeof(tool)};tool.hwnd=m_hwnd;tool.uId=i+1;
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
    m_chromeLabels=std::move(labels);UpdateCaptionTooltips();RefreshCaption();PaintGlassStatus();
}

void Window::LayoutContent()
{
    const auto bounds=GetContentBounds();
    if(m_videoWindow)SetWindowPos(m_videoWindow,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
        SWP_NOZORDER|SWP_NOACTIVATE);
    if(m_videoWindow&&m_composition){
        if(m_glassStatus){auto empty=CreateRectRgn(0,0,0,0);if(!SetWindowRgn(m_videoWindow,empty,TRUE))DeleteObject(empty);}
        else if(IsRectEmpty(&m_panelBackdrop))SetWindowRgn(m_videoWindow,nullptr,TRUE);
        else{
            auto video=CreateRectRgn(0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
            auto panel=CreateRectRgn(m_panelBackdrop.left-bounds.left,m_panelBackdrop.top-bounds.top,
                m_panelBackdrop.right-bounds.left,m_panelBackdrop.bottom-bounds.top);
            CombineRgn(video,video,panel,RGN_DIFF);DeleteObject(panel);
            if(!SetWindowRgn(m_videoWindow,video,TRUE))DeleteObject(video);
        }
    }
    if(!m_composition){
        MARGINS glass{0,0,CaptionHeight(),0};DwmExtendFrameIntoClientArea(m_hwnd,&glass);
    }
    if(m_composition&&!m_glide.active){
        RECT rect{};GetWindowRect(m_hwnd,&rect);
        const SIZE size{rect.right-rect.left,rect.bottom-rect.top};
        if(!CaptionHeight()){
            if(m_regionSize.cx){m_regionSize={};SetWindowRgn(m_hwnd,nullptr,TRUE);}
        }else if(size.cx!=m_regionSize.cx||size.cy!=m_regionSize.cy){
            m_regionSize=size;
            const int diameter=static_cast<int>(std::lround(2*WindowMotion::Radius*GetDpiForWindow(m_hwnd)/96.f));
            auto region=CreateRoundRectRgn(0,0,rect.right-rect.left,rect.bottom-rect.top,diameter,diameter);
            if(!SetWindowRgn(m_hwnd,region,TRUE))DeleteObject(region);
        }
    }
    UpdateGlassPosition();
    UpdateCaptionTooltips();RefreshCaption();
    PaintGlassStatus();
}

void Window::UpdateGlassPosition()
{
    if(!m_composition)return;
    if((!CaptionHeight()&&IsRectEmpty(&m_panelBackdrop)&&!m_glassStatus)||!IsWindowVisible(m_hwnd)||IsIconic(m_hwnd)){m_composition->HideGlass();return;}
    RECT bounds{};GetWindowRect(m_hwnd,&bounds);
    if(m_glide.active)bounds=m_glide.drawn;
    m_composition->PlaceGlass(bounds,GetDpiForWindow(m_hwnd)/96.f);
    if(!CaptionHeight())m_composition->AnimateGlass(true,false);
}

void Window::SetGlassEffect(bool enabled,float strength)
{
    if(m_composition){m_composition->SetGlassEnabled(enabled);m_composition->SetBlurStrength(strength);}
}

void Window::SetPanelBackdrop(RECT bounds)
{
    if(EqualRect(&bounds,&m_panelBackdrop))return;
    m_panelBackdrop=bounds;LayoutContent();
}

void Window::SetGlassStatus(std::optional<GlassStatus> status)
{
    if(status==m_glassStatus)return;
    m_glassStatus=std::move(status);LayoutContent();
}

void Window::PaintGlassStatus()
{
    if(!m_composition)return;
    if(!m_glassStatus){m_composition->HideStatus();return;}
    using namespace Gdiplus;
    auto bounds=GetContentBounds();
    if(!IsRectEmpty(&m_panelBackdrop)){
        if(m_panelBackdrop.left<=bounds.left&&m_panelBackdrop.right>=bounds.right){m_composition->HideStatus();return;}
        if(m_panelBackdrop.left>bounds.left)bounds.right=m_panelBackdrop.left;
        else bounds.left=m_panelBackdrop.right;
    }
    const float scale=std::min(GetDpiForWindow(m_hwnd)/96.f,
        std::min((bounds.right-bounds.left)/552.f,(bounds.bottom-bounds.top)/306.f));
    if(scale<=0)return;
    const int width=static_cast<int>(520*scale),height=static_cast<int>(274*scale);
    Bitmap bitmap(width,height,PixelFormat32bppPARGB);Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Color(0,0,0,0));
    GraphicsPath card;const float d=24*scale,w=static_cast<float>(width-1),h=static_cast<float>(height-1);
    card.AddArc(0.f,0.f,d,d,180,90);card.AddArc(w-d,0.f,d,d,270,90);
    card.AddArc(w-d,h-d,d,d,0,90);card.AddArc(0.f,h-d,d,d,90,90);card.CloseFigure();
    SolidBrush cardTint(Color(24,255,255,255)),ink(Color(245,235,238,242)),muted(Color(205,190,200,213));
    SolidBrush accent(Color(255,117,180,241));Pen rule(Color(25,255,255,255),scale);
    graphics.FillPath(&cardTint,&card);graphics.DrawPath(&rule,&card);
    const auto* fonts=m_chromeLabels.fontFamily==L"Archivo"?m_captionFonts.get():nullptr;
    Font smallFont(m_chromeLabels.fontFamily.c_str(),11*scale,FontStyleRegular,UnitPixel,fonts);
    Font body(m_chromeLabels.fontFamily.c_str(),13*scale,FontStyleRegular,UnitPixel,fonts);
    Font heading(m_chromeLabels.fontFamily.c_str(),23*scale,FontStyleBold,UnitPixel,fonts);
    if(m_captionLogo){
        const float logoH=18*scale,logoW=logoH*m_captionLogo->GetWidth()/m_captionLogo->GetHeight();
        graphics.DrawImage(m_captionLogo.get(),32*scale,28*scale,logoW,logoH);
        graphics.DrawString(HolyBear::Version.data(),static_cast<INT>(HolyBear::Version.size()),&smallFont,
            RectF(44*scale+logoW,31*scale,150*scale,20*scale),nullptr,&muted);
    }
    StringFormat right;right.SetAlignment(StringAlignmentFar);right.SetTrimming(StringTrimmingEllipsisCharacter);
    graphics.DrawString(m_glassStatus->state.c_str(),-1,&smallFont,RectF(280*scale,31*scale,208*scale,22*scale),&right,&muted);
    graphics.FillEllipse(&accent,static_cast<float>(width-24*scale),35*scale,5*scale,5*scale);
    graphics.DrawLine(&rule,32*scale,64*scale,width-32*scale,64*scale);
    graphics.DrawString(m_glassStatus->headline.c_str(),-1,&heading,RectF(32*scale,96*scale,width-64*scale,38*scale),nullptr,&ink);
    graphics.DrawString(m_glassStatus->description.c_str(),-1,&body,RectF(32*scale,140*scale,width-64*scale,48*scale),nullptr,&muted);
    graphics.DrawLine(&rule,32*scale,196*scale,width-32*scale,196*scale);
    graphics.DrawString(m_glassStatus->hint.c_str(),-1,&smallFont,RectF(32*scale,214*scale,width-64*scale,48*scale),nullptr,&muted);
    graphics.Flush();BitmapData data;Rect area(0,0,width,height);
    if(bitmap.LockBits(&area,ImageLockModeRead,PixelFormat32bppPARGB,&data)==Ok){
        m_composition->PaintStatus(data.Scan0,data.Stride,width,height,
            bounds.left+(bounds.right-bounds.left-width)/2,bounds.top+(bounds.bottom-bounds.top-height)/2);
        bitmap.UnlockBits(&data);
    }
}

void Window::InvokeCaptionButton(int index)
{
    if(index==0){if(m_inputCb.onCycleHud)m_inputCb.onCycleHud();}
    else if(index==1){if(m_inputCb.onSettings)m_inputCb.onSettings();}
    else if(index==2)SendMessageW(m_hwnd,WM_SYSCOMMAND,SC_MINIMIZE,0);
    else if(index==3)SendMessageW(m_hwnd,WM_SYSCOMMAND,IsZoomed(m_hwnd)?SC_RESTORE:SC_MAXIMIZE,0);
    else if(index==4)SendMessageW(m_hwnd,WM_SYSCOMMAND,SC_CLOSE,0);
}

void Window::PaintCaption(HDC dc)
{
    using namespace Gdiplus;
    const auto client=LayoutBounds();const int width=client.right-client.left,height=CaptionHeight();
    if(width<=0||height<=0)return;
    const float scale=GetDpiForWindow(m_hwnd)/96.f;
    Bitmap bitmap(width,height,PixelFormat32bppPARGB);Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.Clear(m_composition?Color(0,0,0,0):m_desktopGlass?Color(26,18,18,22):Color(255,30,30,34));
    const BYTE alpha=GetForegroundWindow()==m_hwnd?240:180;
    SolidBrush ink(Color(alpha,245,245,248));Pen line(Color(alpha,235,235,240),1.75f*16/24*scale);
    line.SetStartCap(LineCapRound);line.SetEndCap(LineCapRound);line.SetLineJoin(LineJoinRound);
    wchar_t title[1024]{};GetWindowTextW(m_hwnd,title,1024);
    float textLeft=39*scale;
    const wchar_t* captionText=title;
    if(m_captionLogo){
        const float logoHeight=22*scale,logoWidth=logoHeight*m_captionLogo->GetWidth()/m_captionLogo->GetHeight();
        graphics.DrawImage(m_captionLogo.get(),12*scale,(height-logoHeight)/2,logoWidth,logoHeight);
        textLeft=12*scale+logoWidth+12*scale;
        // The lockup already spells NitLink; keep the live source/game suffix.
        if(std::wstring_view(title).starts_with(L"NitLink - "))captionText+=10;
        else if(std::wstring_view(title)==L"NitLink")captionText+=7;
    }else{
        HICON icon=reinterpret_cast<HICON>(SendMessageW(m_hwnd,WM_GETICON,ICON_SMALL,0));
        if(!icon)icon=LoadIconW(m_hInstance,MAKEINTRESOURCEW(IDI_APP_ICON));
        if(icon){Bitmap brand(icon);graphics.DrawImage(&brand,12*scale,(height-18*scale)/2,18*scale,18*scale);}
    }
    Font font(m_chromeLabels.fontFamily.c_str(),15*scale,FontStyleRegular,UnitPixel,
        m_chromeLabels.fontFamily==L"Archivo"?m_captionFonts.get():nullptr);StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap);format.SetTrimming(StringTrimmingEllipsisCharacter);format.SetLineAlignment(StringAlignmentCenter);
    const auto first=CaptionButtonRect(m_isSettingsWindow?2:0);
    RectF titleRect(textLeft,0,std::max(0.f,first.left-client.left-textLeft-10*scale),static_cast<float>(height));
    if(!m_isSettingsWindow)graphics.DrawString(captionText,-1,&font,titleRect,&format,&ink);
    for(int i=m_isSettingsWindow?2:0;i<5;++i){
        auto rect=CaptionButtonRect(i);OffsetRect(&rect,-client.left,-client.top);
        const float cx=(rect.left+rect.right)/2.f,cy=height/2.f;
        if(m_captionHover==i||m_captionPressed==i||(i==1&&m_settingsVisible)){
            SolidBrush hover(i==4?Color(220,196,43,45):i==1&&m_settingsVisible?Color(65,100,170,230):Color(m_captionPressed==i?55:30,255,255,255));
            GraphicsPath outline;const float d=16*scale;
            outline.AddArc(static_cast<float>(rect.left),static_cast<float>(rect.top),d,d,180,90);
            outline.AddArc(rect.right-d,static_cast<float>(rect.top),d,d,270,90);
            outline.AddArc(rect.right-d,rect.bottom-d,d,d,0,90);
            outline.AddArc(static_cast<float>(rect.left),rect.bottom-d,d,d,90,90);outline.CloseFigure();
            graphics.FillPath(&hover,&outline);
        }
        if(i==0){
            Pen hud(Color(m_hudVisible?alpha:105,235,235,240),1.75f*16/24*scale);
            hud.SetStartCap(LineCapRound);hud.SetEndCap(LineCapRound);hud.SetLineJoin(LineJoinRound);
            if(m_hudMode==0){graphics.DrawRectangle(&hud,cx-8*scale,cy-6*scale,16*scale,12*scale);graphics.DrawLine(&hud,cx-5*scale,cy-2*scale,cx+5*scale,cy-2*scale);graphics.DrawLine(&hud,cx-5*scale,cy+2*scale,cx+2*scale,cy+2*scale);}
            else if(m_hudMode==1){for(int row=0;row<3;++row){const float y=cy+(row-1)*4*scale;graphics.DrawLine(&hud,cx-8*scale,y,cx-5*scale,y);graphics.DrawLine(&hud,cx-2*scale,y,cx+8*scale,y);}}
            else {graphics.DrawLine(&hud,cx-8*scale,cy,cx-4*scale,cy);graphics.DrawLine(&hud,cx-1*scale,cy,cx+3*scale,cy);graphics.DrawLine(&hud,cx+6*scale,cy,cx+9*scale,cy);}
        }else if(i==1){
            PointF points[24];
            for(int vertex=0;vertex<24;++vertex){
                const float angle=vertex*3.14159265f/12.f;
                const float radius=(vertex%4==0||vertex%4==3?7.f:5.5f)*scale;
                points[vertex]={cx+std::cos(angle)*radius,cy+std::sin(angle)*radius};
            }
            graphics.DrawPolygon(&line,points,24);graphics.DrawEllipse(&line,cx-2*scale,cy-2*scale,4*scale,4*scale);
        }else if(i==2)graphics.DrawLine(&line,cx-14.f/3*scale,cy,cx+14.f/3*scale,cy);
        else if(i==3){
            if(IsZoomed(m_hwnd)){
                graphics.DrawLine(&line,cx-2*scale,cy-5*scale,cx+5*scale,cy-5*scale);
                graphics.DrawLine(&line,cx+5*scale,cy-5*scale,cx+5*scale,cy+2*scale);
            }
            const float side=(IsZoomed(m_hwnd)?8.f:28.f/3)*scale,d=8.f/3*scale;
            const float x=cx-side/2-(IsZoomed(m_hwnd)?scale:0),y=cy-side/2+(IsZoomed(m_hwnd)?scale:0);
            GraphicsPath box;
            box.AddArc(x,y,d,d,180,90);box.AddArc(x+side-d,y,d,d,270,90);
            box.AddArc(x+side-d,y+side-d,d,d,0,90);box.AddArc(x,y+side-d,d,d,90,90);box.CloseFigure();
            graphics.DrawPath(&line,&box);
        }
        else {graphics.DrawLine(&line,cx-4*scale,cy-4*scale,cx+4*scale,cy+4*scale);graphics.DrawLine(&line,cx+4*scale,cy-4*scale,cx-4*scale,cy+4*scale);}
    }
    graphics.Flush();
    BitmapData data;Rect area(0,0,width,height);
    if(bitmap.LockBits(&area,ImageLockModeRead,PixelFormat32bppPARGB,&data)==Ok){
        if(m_composition)m_composition->PaintCaption(data.Scan0,data.Stride,width,height,client.left,client.top);
        else{
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            SetDIBitsToDevice(dc,client.left,client.top,width,height,0,0,0,height,data.Scan0,&info,DIB_RGB_COLORS);
        }
        bitmap.UnlockBits(&data);
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
    StopMotion();
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
        // The separate glass keeps owning corners after fullscreen returns.
        DWORD cornerPref = m_composition?1:0;
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
    if(!fullscreen&&m_composition&&CaptionHeight())m_composition->AnimateGlass(true,false);
}

void Window::SetPiP(bool enabled, uint32_t width, uint32_t height, float opacity)
{
    if (enabled == m_isPiP) return; // No-op if already in target state
    StopMotion();
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
    if(!enabled&&m_composition&&CaptionHeight())m_composition->AnimateGlass(true,false);
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
        if(msg==WM_NCHITTEST&&self->CaptionHeight()){
            const auto hit=SendMessageW(self->m_hwnd,msg,wParam,lParam);
            if(hit!=HTCLIENT)return HTTRANSPARENT;
        }
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
    case WM_TIMER:
        if(self&&wParam==kWindowMotionTimer){self->AdvanceMotion();return 0;}
        break;
    case WM_NCCALCSIZE:
        // The entire window is our client, including all resize grips.
        // Windows must never reserve or paint a native caption above it.
        return 0;
    case WM_NCPAINT:
        if(self&&self->m_composition)return 0;
        break;
    case WM_NCACTIVATE:
        if(self&&self->m_composition)return TRUE;
        break;
    case WM_NCHITTEST:
        if(self&&self->CaptionHeight()){
            POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};ScreenToClient(hwnd,&point);
            if(self->CaptionButtonAt(point)>=0)return HTCLIENT;
            const auto client=self->LayoutBounds();const UINT dpi=GetDpiForWindow(hwnd);
            const int edge=MulDiv(8,dpi,96),corner=MulDiv(24,dpi,96);
            if(!IsZoomed(hwnd)&&!self->m_glide.active){
                const bool left=point.x<corner,right=point.x>=client.right-corner;
                if(point.y<corner&&left)return HTTOPLEFT;
                if(point.y<corner&&right)return HTTOPRIGHT;
                if(point.y>=client.bottom-corner&&left)return HTBOTTOMLEFT;
                if(point.y>=client.bottom-corner&&right)return HTBOTTOMRIGHT;
                if(point.y<edge)return HTTOP;
                if(point.y>=client.bottom-edge)return HTBOTTOM;
                if(point.x<edge)return HTLEFT;
                if(point.x>=client.right-edge)return HTRIGHT;
            }
            if(point.y>=client.top&&point.y<client.top+self->CaptionHeight()&&point.x>=client.left&&point.x<client.right)return HTCAPTION;
            return HTCLIENT;
        }
        break;
    case WM_NCLBUTTONDBLCLK:
        if(self&&wParam==HTCAPTION&&self->CaptionHeight()){
            SendMessageW(hwnd,WM_SYSCOMMAND,IsZoomed(hwnd)?SC_RESTORE:SC_MAXIMIZE,0);return 0;
        }
        break;
    case WM_PAINT:
        if(self){PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);self->PaintCaption(dc);EndPaint(hwnd,&paint);return 0;}
        break;
    case WM_DPICHANGED:
        if(self){self->StopMotion();const auto* rect=reinterpret_cast<RECT*>(lParam);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOACTIVATE|SWP_NOZORDER);self->LayoutContent();return 0;}
        break;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        if(self&&self->m_composition)self->m_composition->RefreshTheme();
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
            if (wParam == SIZE_MINIMIZED) {
                self->m_glassHiddenByMinimize = true;
                self->m_playbackPower.SetActive(false);
                if(self->m_composition)self->m_composition->HideGlass();
            }
            else self->UpdatePlaybackPower();
        }
        // Set the resize flag for any size change EXCEPT minimize (which
        // gives 0x0 dimensions and would crash the renderer)
        if (self && wParam != SIZE_MINIMIZED) {
            self->LayoutContent();
            if(self->m_glassHiddenByMinimize){
                self->m_glassHiddenByMinimize=false;
                if(!self->m_restoringIconic&&self->m_composition&&self->CaptionHeight())
                    self->m_composition->AnimateGlass(true,false);
            }
            if(self->m_onMove)self->m_onMove();
            self->m_wasResized = true;
            // Only the normal window size belongs to the next windowed launch.
            if (wParam == SIZE_RESTORED && !self->m_isFullscreen && !self->m_isPiP && !self->m_glide.active) {
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
            if(!wParam&&self->m_composition)self->m_composition->HideGlass();
        }
        break;

    case WM_SYSCOMMAND:
        if(self){
            const UINT command=static_cast<UINT>(wParam)&0xfff0;
            if(self->ShouldAnimate()&&(command==SC_MINIMIZE||command==SC_CLOSE)&&!IsIconic(hwnd)){
                if(self->m_fade.active&&self->m_fade.completionCommand==command)return 0;
                self->EndGlide();self->BeginFade(false,command);return 0;
            }
            if(self->m_composition&&self->CaptionHeight()){
                const bool iconic=IsIconic(hwnd)!=FALSE;
                if((command==SC_MAXIMIZE&&(!IsZoomed(hwnd)||iconic))||
                    (command==SC_RESTORE&&(IsZoomed(hwnd)||iconic))){
                    self->StopMotion();
                    RECT from{};GetWindowRect(hwnd,&from);
                    const UINT dpi=GetDpiForWindow(hwnd);
                    if(iconic){
                        SetWindowLongW(hwnd,GWL_EXSTYLE,GetWindowLongW(hwnd,GWL_EXSTYLE)|WS_EX_LAYERED);
                        SetLayeredWindowAttributes(hwnd,0,0,LWA_ALPHA);self->m_contentOpacity=0;
                    }
                    // Establish the final layout once; renderer dimensions do
                    // not change on the following clipped boundary frames.
                    self->m_restoringIconic=iconic;
                    const auto result=DefWindowProcW(hwnd,msg,wParam,lParam);
                    self->m_restoringIconic=false;
                    RECT to{};GetWindowRect(hwnd,&to);
                    // Native placement may hide/show the host during the
                    // transition. Restore its separate material as well.
                    if(!iconic)self->m_composition->AnimateGlass(true,false);
                    if(iconic)self->BeginFade(true);
                    else if(self->ShouldAnimate()&&dpi==GetDpiForWindow(hwnd)&&!EqualRect(&from,&to)){
                        RECT client{},host{};GetClientRect(hwnd,&client);UnionRect(&host,&from,&to);
                        self->m_glide={true,from,to,host,from,{client.right,client.bottom},GetTickCount64()};
                        SetWindowPos(hwnd,nullptr,host.left,host.top,host.right-host.left,host.bottom-host.top,
                            SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
                        self->LayoutContent();UpdateWindow(hwnd);
                        SetTimer(hwnd,kWindowMotionTimer,16,nullptr);self->AdvanceMotion();
                    }
                    return result;
                }
            }
        }
        if (self && self->m_playbackPower.SuppressesScreenSaver(wParam)) return 0;
        break;

    case WM_WINDOWPOSCHANGING:
        if(self&&self->m_composition&&!self->m_glide.active&&IsWindowVisible(hwnd)&&!IsIconic(hwnd)){
            const auto* position=reinterpret_cast<const WINDOWPOS*>(lParam);
            if(position&&(position->flags&SWP_NOSIZE)&&!(position->flags&SWP_NOMOVE)){
                RECT current{};GetWindowRect(hwnd,&current);
                self->m_composition->PlaceGlass({position->x,position->y,
                    position->x+current.right-current.left,position->y+current.bottom-current.top},GetDpiForWindow(hwnd)/96.f);
            }
        }
        break;

    case WM_WINDOWPOSCHANGED:
        if(self&&!IsIconic(hwnd))self->UpdateGlassPosition();
        break;

    case WM_MOVE:
        // Native dragging runs a modal message loop. Notify the child browser
        // here, while deferred monitor detection waits for the application loop.
        if (self) {
            self->m_wasMoved = true;
            if (self->m_onMove) self->m_onMove();
        }
        return 0;

    case WM_ENTERSIZEMOVE:
        if(self)self->StopMotion();
        return 0;

    case WM_EXITSIZEMOVE:
        // Reconcile the picture and child browser at the final dragged size.
        if (self) {
            self->m_wasResized = true;
            if (self->m_onMove) self->m_onMove();
        }
        return 0;

    case WM_ACTIVATE:
        if(self){self->RefreshCaption();if(LOWORD(wParam)!=WA_INACTIVE)self->UpdateGlassPosition();}
        // Reconcile child-controller position when activation returns.
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
            if(self&&!self->m_isFullscreen&&!self->m_isPiP){
                MONITORINFO monitor{sizeof(monitor)};
                GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
                mmi->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
                mmi->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};
            }
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
            const auto bounds=self->GetContentBounds();
            int x = (short)LOWORD(lParam)-bounds.left;
            int y = (short)HIWORD(lParam)-bounds.top;
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
            const auto bounds=self->GetContentBounds();
            int x = (short)LOWORD(lParam)-bounds.left;
            int y = (short)HIWORD(lParam)-bounds.top;
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
            const auto bounds=self->GetContentBounds();
            int x = (short)LOWORD(lParam)-bounds.left;
            int y = (short)HIWORD(lParam)-bounds.top;
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
        if(self){
            self->EndGlide();self->m_fade.active=false;KillTimer(hwnd,kWindowMotionTimer);
            if(self->m_composition)self->m_composition->HideGlass();
        }
        // Pending settings are committed while the child browser is still alive.
        if (self && self->m_onClose) self->m_onClose();
        if(self&&self->m_isSettingsWindow){ShowWindow(hwnd,SW_HIDE);return 0;}
        break;

    case WM_DESTROY:
        if (self) {
            self->SetVideoAvailable(false);
            if(self->m_composition)self->m_composition->HideGlass();
        }
        if(!self||!self->m_isSettingsWindow)PostQuitMessage(0);
        return 0;

    case WM_ERASEBKGND:
        return 1; // Prevent flicker
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace NitLink
