#pragma once
#include <algorithm>
#include <array>

namespace NitLink::WindowMotion {
// Visual motion tokens: separate glass spring, content fade, and bounds glide.
// Reference: Harbor a56ab11, Controls/Motion.cs and Glass/GlassSurface.cs.
inline constexpr unsigned FastMs=120, NormalMs=220, SlowMs=360, GlideMs=180;
inline constexpr float EnterScale=.94f, Radius=20.f;
inline constexpr std::array<float,4> Smooth{.2f,0.f,0.f,1.f};
inline constexpr std::array<float,4> Spring{.34f,1.36f,.64f,1.f};
inline constexpr std::array<float,4> Glide{.25f,1.05f,.35f,1.f};

inline double Progress(const std::array<float,4>& curve,double time)
{
    time=std::clamp(time,0.,1.);
    double parameter=time;
    // Solve the curve's time coordinate, then evaluate its value coordinate.
    for(unsigned i=0;i<8;++i){
        const double other=1-parameter;
        const double x=3*other*other*parameter*curve[0]+3*other*parameter*parameter*curve[2]+parameter*parameter*parameter;
        const double slope=3*other*other*curve[0]+6*other*parameter*(curve[2]-curve[0])+3*parameter*parameter*(1-curve[2]);
        if(slope<1e-6)break;
        parameter=std::clamp(parameter-(x-time)/slope,0.,1.);
    }
    const double other=1-parameter;
    return 3*other*other*parameter*curve[1]+3*other*parameter*parameter*curve[3]+parameter*parameter*parameter;
}
}
