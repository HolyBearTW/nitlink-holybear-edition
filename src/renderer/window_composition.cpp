#include "window_composition.h"
#include "window_motion.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <windows.graphics.effects.interop.h>
#include <d2d1effects.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Core.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <array>
#include <cmath>
#include <chrono>
#pragma comment(lib,"windowsapp.lib")
#pragma comment(lib,"CoreMessaging.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dxguid.lib")

namespace NitLink {
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Graphics::Effects;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Composition;
using Microsoft::WRL::ComPtr;
namespace EffectAbi=ABI::Windows::Graphics::Effects;

namespace {
// Standard Direct2D effect interop; no Win2D or WinUI runtime dependency.
struct BackdropSaturation : implements<BackdropSaturation,IGraphicsEffect,
    IGraphicsEffectSource,EffectAbi::IGraphicsEffectD2D1Interop> {
    hstring m_name=L"BackdropSaturation";
    IGraphicsEffectSource m_source;
    explicit BackdropSaturation(IGraphicsEffectSource source):m_source(std::move(source)){}
    hstring Name() const {return m_name;}
    void Name(hstring const& value){m_name=value;}
    HRESULT __stdcall GetEffectId(GUID* id) noexcept override {*id=CLSID_D2D1Saturation;return S_OK;}
    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR name,UINT* index,EffectAbi::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept override {
        if(wcscmp(name,L"Saturation"))return E_INVALIDARG;
        *index=0;*mapping=EffectAbi::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;return S_OK;
    }
    HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override {*count=1;return S_OK;}
    HRESULT __stdcall GetProperty(UINT index,ABI::Windows::Foundation::IPropertyValue** value) noexcept override {
        if(index)return E_INVALIDARG;
        try {auto property=PropertyValue::CreateSingle(1.6f);*value=reinterpret_cast<ABI::Windows::Foundation::IPropertyValue*>(detach_abi(property));return S_OK;}
        catch(...){return to_hresult();}
    }
    HRESULT __stdcall GetSource(UINT index,EffectAbi::IGraphicsEffectSource** source) noexcept override {
        if(index)return E_INVALIDARG;
        *source=reinterpret_cast<EffectAbi::IGraphicsEffectSource*>(get_abi(m_source));(*source)->AddRef();return S_OK;
    }
    HRESULT __stdcall GetSourceCount(UINT* count) noexcept override {*count=1;return S_OK;}
};

Color GlassColor(float opacity,BYTE r,BYTE g,BYTE b)
{
    return Color{static_cast<BYTE>(std::lround(opacity*255)),r,g,b};
}

LRESULT CALLBACK GlassProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
{
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(window,message,wParam,lParam);
}
}

struct WindowComposition::Impl {
    // Keep WinRT initialized until every compositor object has been released.
    Microsoft::WRL::Wrappers::RoInitializeWrapper runtime{RO_INIT_SINGLETHREADED};
    HWND content=nullptr,glass=nullptr;
    winrt::Windows::System::DispatcherQueueController queue{nullptr};
    winrt::Windows::UI::Composition::Core::CompositorController controller{nullptr};
    Compositor compositor{nullptr};
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget contentTarget{nullptr},glassTarget{nullptr};
    ContainerVisual contentRoot{nullptr},glassRoot{nullptr};
    SpriteVisual caption{nullptr},material{nullptr},luminosity{nullptr},tint{nullptr};
    std::array<SpriteVisual,2> shadows{nullptr,nullptr};
    CompositionRoundedRectangleGeometry maskGeometry{nullptr},clipGeometry{nullptr},outerGeometry{nullptr},innerGeometry{nullptr};
    ShapeVisual mask{nullptr},edges{nullptr};
    CompositionVisualSurface maskSurface{nullptr};
    winrt::Windows::UI::ViewManagement::UISettings settings{nullptr};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> captionChain;
    unsigned captionWidth=0,captionHeight=0;
    float2 glassSize{};
    bool solid=false;

    ~Impl(){
        if(contentTarget){contentTarget.Root(nullptr);contentTarget.Close();}
        if(glassTarget){glassTarget.Root(nullptr);glassTarget.Close();}
        if(glass)DestroyWindow(glass);
    }

    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget Target(HWND window){
        winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
        check_hresult(compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>()->
            CreateDesktopWindowTarget(window,FALSE,reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(put_abi(target))));
        return target;
    }

    void TitleSurface(unsigned width,unsigned height){
        if(captionChain){
            check_hresult(captionChain->ResizeBuffers(2,width,height,DXGI_FORMAT_B8G8R8A8_UNORM,0));
        }else{
            ComPtr<IDXGIDevice> dxgi;check_hresult(device.As(&dxgi));
            ComPtr<IDXGIAdapter> adapter;check_hresult(dxgi->GetAdapter(&adapter));
            ComPtr<IDXGIFactory2> factory;check_hresult(adapter->GetParent(IID_PPV_ARGS(&factory)));
            DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
            desc.Scaling=DXGI_SCALING_STRETCH;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
            check_hresult(factory->CreateSwapChainForComposition(device.Get(),&desc,nullptr,&captionChain));
            ICompositionSurface surface{nullptr};
            check_hresult(compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>()->
                CreateCompositionSurfaceForSwapChain(captionChain.Get(),reinterpret_cast<ABI::Windows::UI::Composition::ICompositionSurface**>(put_abi(surface))));
            caption.Brush(compositor.CreateSurfaceBrush(surface));
        }
        captionWidth=width;captionHeight=height;
    }
};

WindowComposition::WindowComposition():m_impl(std::make_unique<Impl>()){}
WindowComposition::~WindowComposition()=default;

bool WindowComposition::Initialize(HWND content)
{
    auto& p=*m_impl;p.content=content;
    const LONG originalStyle=GetWindowLongW(content,GWL_EXSTYLE);
    try {
        check_hresult(static_cast<HRESULT>(p.runtime));
        if(!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()){
            DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
            check_hresult(CreateDispatcherQueueController(options,reinterpret_cast<PDISPATCHERQUEUECONTROLLER*>(put_abi(p.queue))));
        }
        p.controller=winrt::Windows::UI::Composition::Core::CompositorController();p.compositor=p.controller.Compositor();
        p.settings=winrt::Windows::UI::ViewManagement::UISettings();
        D3D_FEATURE_LEVEL level{};
        check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&p.device,&level,&p.context));
        SetWindowLongW(content,GWL_EXSTYLE,originalStyle|WS_EX_NOREDIRECTIONBITMAP);
        p.contentTarget=p.Target(content);p.contentRoot=p.compositor.CreateContainerVisual();
        p.contentRoot.RelativeSizeAdjustment({1,1});p.caption=p.compositor.CreateSpriteVisual();
        p.contentRoot.Children().InsertAtTop(p.caption);p.contentTarget.Root(p.contentRoot);

        WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=GlassProc;cls.lpszClassName=L"NitLinkGlassBackdrop";
        WNDCLASSEXW registered{sizeof(registered)};
        if(!GetClassInfoExW(cls.hInstance,cls.lpszClassName,&registered))check_bool(RegisterClassExW(&cls));
        p.glass=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP|WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,
            cls.lpszClassName,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,cls.hInstance,nullptr);
        check_bool(p.glass);SetLayeredWindowAttributes(p.glass,0,255,LWA_ALPHA);
        BOOL host=TRUE;check_hresult(DwmSetWindowAttribute(p.glass,17,&host,sizeof(host)));
        DWORD corner=1;DwmSetWindowAttribute(p.glass,33,&corner,sizeof(corner));
        p.glassTarget=p.Target(p.glass);p.glassRoot=p.compositor.CreateContainerVisual();p.glassTarget.Root(p.glassRoot);

        p.maskGeometry=p.compositor.CreateRoundedRectangleGeometry();p.mask=p.compositor.CreateShapeVisual();
        auto maskShape=p.compositor.CreateSpriteShape(p.maskGeometry);maskShape.FillBrush(p.compositor.CreateColorBrush(GlassColor(1,255,255,255)));
        p.mask.Shapes().Append(maskShape);p.maskSurface=p.compositor.CreateVisualSurface();p.maskSurface.SourceVisual(p.mask);
        auto maskBrush=p.compositor.CreateSurfaceBrush(p.maskSurface);
        const float3 inset{48,48,0};
        for(unsigned i=0;i<2;++i){
            p.shadows[i]=p.compositor.CreateSpriteVisual();p.shadows[i].Offset(inset);
            auto shadow=p.compositor.CreateDropShadow();shadow.Mask(maskBrush);shadow.Color(GlassColor(1,0,0,0));
            shadow.BlurRadius(i==0?44.8f:8.f);shadow.Offset({0,i==0?16.8f:2.f,0});shadow.Opacity(i==0?.224f:.084f);
            p.shadows[i].Shadow(shadow);p.glassRoot.Children().InsertAtTop(p.shadows[i]);
        }
        p.clipGeometry=p.compositor.CreateRoundedRectangleGeometry();
        p.material=p.compositor.CreateSpriteVisual();p.luminosity=p.compositor.CreateSpriteVisual();p.tint=p.compositor.CreateSpriteVisual();
        for(const auto& layer:{p.material,p.luminosity,p.tint}){
            layer.Offset(inset);layer.Clip(p.compositor.CreateGeometricClip(p.clipGeometry));p.glassRoot.Children().InsertAtTop(layer);
        }
        p.luminosity.Brush(p.compositor.CreateColorBrush(GlassColor(.38f,10,10,12)));
        p.tint.Brush(p.compositor.CreateColorBrush(GlassColor(.42f,28,28,32)));
        p.edges=p.compositor.CreateShapeVisual();p.edges.Offset(inset);
        p.outerGeometry=p.compositor.CreateRoundedRectangleGeometry();p.outerGeometry.Offset({-.5f,-.5f});
        auto outer=p.compositor.CreateSpriteShape(p.outerGeometry);outer.StrokeThickness(1);outer.StrokeBrush(p.compositor.CreateColorBrush(GlassColor(0,0,0,0)));
        p.innerGeometry=p.compositor.CreateRoundedRectangleGeometry();p.innerGeometry.Offset({.5f,.5f});
        auto gradient=p.compositor.CreateLinearGradientBrush();gradient.StartPoint({0,0});gradient.EndPoint({0,1});
        constexpr float stops[4]={0,.3f,.7f,1},strengths[4]={1,.6f,.45f,.65f};
        for(unsigned i=0;i<4;++i)gradient.ColorStops().Append(p.compositor.CreateColorGradientStop(stops[i],GlassColor(.154f*strengths[i],255,255,255)));
        auto inner=p.compositor.CreateSpriteShape(p.innerGeometry);inner.StrokeThickness(1);inner.StrokeBrush(gradient);
        p.edges.Shapes().Append(outer);p.edges.Shapes().Append(inner);p.glassRoot.Children().InsertAtTop(p.edges);
        p.glassRoot.Opacity(0);p.glassRoot.Scale({WindowMotion::EnterScale,WindowMotion::EnterScale,1});
        RefreshTheme();p.controller.Commit();return true;
    }catch(const hresult_error& error){
        OutputDebugStringW((L"[NitLink/Window] composition unavailable: "+std::to_wstring(error.code().value)+L"\n").c_str());
        SetWindowLongW(content,GWL_EXSTYLE,originalStyle);return false;
    }
}

void WindowComposition::RefreshTheme()
{
    auto& p=*m_impl;
    winrt::Windows::UI::ViewManagement::AccessibilitySettings accessibility;
    p.solid=!p.settings.AdvancedEffectsEnabled()||accessibility.HighContrast();
    if(p.solid)p.material.Brush(p.compositor.CreateColorBrush(GlassColor(1,22,22,25)));
    else{
        CompositionEffectSourceParameter source(L"Backdrop");
        auto effect=make<BackdropSaturation>(source);
        auto brush=p.compositor.CreateEffectFactory(effect).CreateBrush();
        brush.SetSourceParameter(L"Backdrop",p.compositor.CreateHostBackdropBrush());p.material.Brush(brush);
    }
    p.luminosity.Opacity(p.solid?0.f:1.f);p.tint.Opacity(p.solid?0.f:1.f);
    p.controller.Commit();
}

void WindowComposition::PaintCaption(const void* pixels,unsigned pitch,unsigned width,unsigned height,int x,int y)
{
    auto& p=*m_impl;
    if(width!=p.captionWidth||height!=p.captionHeight)p.TitleSurface(width,height);
    ComPtr<ID3D11Texture2D> buffer;check_hresult(p.captionChain->GetBuffer(0,IID_PPV_ARGS(&buffer)));
    p.context->UpdateSubresource(buffer.Get(),0,nullptr,pixels,pitch,0);
    check_hresult(p.captionChain->Present(0,0));p.caption.Offset({static_cast<float>(x),static_cast<float>(y),0});
    p.caption.Size({static_cast<float>(width),static_cast<float>(height)});p.controller.Commit();
}

void WindowComposition::PlaceGlass(RECT bounds,float dpi)
{
    auto& p=*m_impl;const float2 size{(bounds.right-bounds.left)/dpi,(bounds.bottom-bounds.top)/dpi};
    if(size!=p.glassSize){
        p.glassSize=size;p.glassRoot.Size({size.x+96,size.y+112});
        p.maskGeometry.Size(size);p.maskGeometry.CornerRadius({20,20});p.clipGeometry.Size(size);p.clipGeometry.CornerRadius({20,20});
        p.mask.Size(size);p.maskSurface.SourceSize(size);
        for(const auto& layer:{p.shadows[0],p.shadows[1],p.material,p.luminosity,p.tint})layer.Size(size);
        p.edges.Size({size.x+2,size.y+2});p.outerGeometry.Size({size.x+1,size.y+1});p.outerGeometry.CornerRadius({20.5f,20.5f});
        p.innerGeometry.Size({size.x-1,size.y-1});p.innerGeometry.CornerRadius({19.5f,19.5f});
        p.glassRoot.CenterPoint({48+size.x/2,48+size.y/2,0});
    }
    p.glassRoot.TransformMatrix({dpi,0,0,0,0,dpi,0,0,0,0,1,0,0,0,0,1});
    const int margin=static_cast<int>(std::lround(48*dpi)),bottom=static_cast<int>(std::lround(64*dpi));
    SetWindowPos(p.glass,p.content,bounds.left-margin,bounds.top-margin,bounds.right-bounds.left+2*margin,
        bounds.bottom-bounds.top+margin+bottom,SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    p.controller.Commit();
}

void WindowComposition::AnimateGlass(bool show,bool animate)
{
    auto& p=*m_impl;
    if(show)ShowWindow(p.glass,SW_SHOWNOACTIVATE);
    if(!animate){p.glassRoot.StopAnimation(L"Scale");p.glassRoot.StopAnimation(L"Opacity");p.glassRoot.Scale({show?1.f:WindowMotion::EnterScale,show?1.f:WindowMotion::EnterScale,1});p.glassRoot.Opacity(show?1.f:0.f);}
    else{
        const auto& scaleCurve=show?WindowMotion::Spring:WindowMotion::Smooth;
        auto scale=p.compositor.CreateVector3KeyFrameAnimation();scale.InsertExpressionKeyFrame(0,L"this.StartingValue");
        scale.InsertKeyFrame(1,float3{show?1.f:WindowMotion::EnterScale,show?1.f:WindowMotion::EnterScale,1},
            p.compositor.CreateCubicBezierEasingFunction({scaleCurve[0],scaleCurve[1]},{scaleCurve[2],scaleCurve[3]}));
        scale.Duration(std::chrono::milliseconds(show?WindowMotion::SlowMs:WindowMotion::FastMs));
        auto fade=p.compositor.CreateScalarKeyFrameAnimation();fade.InsertExpressionKeyFrame(0,L"this.StartingValue");
        fade.InsertKeyFrame(1,show?1.f:0.f,p.compositor.CreateCubicBezierEasingFunction({.2f,0},{0,1}));
        fade.Duration(std::chrono::milliseconds(show?WindowMotion::NormalMs:WindowMotion::FastMs));
        p.glassRoot.StartAnimation(L"Scale",scale);p.glassRoot.StartAnimation(L"Opacity",fade);
    }
    p.controller.Commit();
}

void WindowComposition::HideGlass()
{
    auto& p=*m_impl;
    p.glassRoot.StopAnimation(L"Scale");p.glassRoot.StopAnimation(L"Opacity");
    p.glassRoot.Scale({WindowMotion::EnterScale,WindowMotion::EnterScale,1});p.glassRoot.Opacity(0);
    ShowWindow(p.glass,SW_HIDE);p.controller.Commit();
}
bool WindowComposition::AnimationsEnabled() const{return m_impl->settings.AnimationsEnabled();}
}
