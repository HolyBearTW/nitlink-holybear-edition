#pragma once
#include <windows.h>
#include <memory>
#include <cstdint>

namespace NitLink {
// Independent system-compositor glass behind the content window. The game
// keeps its existing HWND swap chain; only the native caption uses a UI surface.
class WindowComposition {
public:
    WindowComposition();
    ~WindowComposition();
    bool Initialize(HWND content);
    void PaintCaption(const void* pixels,unsigned pitch,unsigned width,unsigned height,int x,int y);
    void PaintStatus(const void* pixels,unsigned pitch,unsigned width,unsigned height,int x,int y);
    void HideStatus();
    void PlaceGlass(RECT bounds,float dpi);
    void AnimateGlass(bool show,bool animate);
    void HideGlass();
    void RefreshTheme();
    void SetBlurStrength(float strength);
    void SetGlassEnabled(bool enabled);
    bool AnimationsEnabled() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
