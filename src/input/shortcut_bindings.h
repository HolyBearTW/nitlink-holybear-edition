#pragma once
#include <windows.h>
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace NitLink {
struct ShortcutChord {
    unsigned key=0, modifiers=0; // Ctrl=1, Alt=2, Shift=4; one main key.
    constexpr bool operator==(const ShortcutChord&) const = default;
};
enum class ShortcutAction : size_t {
    toggle_fullscreen,
    toggle_fullscreen_f11,
    toggle_pip,
    pip_nudge_left,
    pip_nudge_right,
    pip_nudge_up,
    pip_nudge_down,
    pip_nudge_left_big,
    pip_nudge_right_big,
    pip_nudge_up_big,
    pip_nudge_down_big,
    pip_resize_narrower,
    pip_resize_wider,
    pip_resize_shorter,
    pip_resize_taller,
    pip_resize_narrower_big,
    pip_resize_wider_big,
    pip_resize_shorter_big,
    pip_resize_taller_big,
    pip_scale_up,
    pip_scale_down,
    pip_scale_up_big,
    pip_scale_down_big,
    pip_opacity_down,
    pip_opacity_up,
    pip_opacity_down_big,
    pip_opacity_up_big,
    screenshot,
    toggle_overlay,
    toggle_fps_details,
    toggle_settings,
    toggle_hdr,
    toggle_hdr_diag,
    toggle_vsync,
    toggle_low_latency,
    cycle_aspect,
    cycle_color_range,
    capture_placeholder_fingerprint,
    toggle_levels_diag,
    Count
};
enum class ShortcutInputContext { Normal, TextInput, Recording };
struct ShortcutDefinition {
    std::string_view id;
    std::wstring_view label,detail;
    bool largeStep;
    unsigned group;
    ShortcutChord defaults;
};
inline constexpr std::array<ShortcutDefinition,static_cast<size_t>(ShortcutAction::Count)> ShortcutDefinitions={{
    {"toggle_fullscreen",L"shortcut.fullscreen",L"",false,0,{VK_RETURN,2}},
    {"toggle_fullscreen_f11",L"shortcut.fullscreen",L"shortcut.alternate",false,0,{VK_F11,0}},
    {"toggle_pip",L"shortcut.pictureInPicture",L"",false,0,{'O',2}},
    {"pip_nudge_left",L"shortcut.editNudge",L"shortcut.left",false,1,{VK_LEFT,1}},
    {"pip_nudge_right",L"shortcut.editNudge",L"shortcut.right",false,1,{VK_RIGHT,1}},
    {"pip_nudge_up",L"shortcut.editNudge",L"shortcut.up",false,1,{VK_UP,1}},
    {"pip_nudge_down",L"shortcut.editNudge",L"shortcut.down",false,1,{VK_DOWN,1}},
    {"pip_nudge_left_big",L"shortcut.editNudge",L"shortcut.left",true,1,{VK_LEFT,5}},
    {"pip_nudge_right_big",L"shortcut.editNudge",L"shortcut.right",true,1,{VK_RIGHT,5}},
    {"pip_nudge_up_big",L"shortcut.editNudge",L"shortcut.up",true,1,{VK_UP,5}},
    {"pip_nudge_down_big",L"shortcut.editNudge",L"shortcut.down",true,1,{VK_DOWN,5}},
    {"pip_resize_narrower",L"shortcut.editWidth",L"shortcut.decrease",false,1,{VK_LEFT,3}},
    {"pip_resize_wider",L"shortcut.editWidth",L"shortcut.increase",false,1,{VK_RIGHT,3}},
    {"pip_resize_shorter",L"shortcut.editHeight",L"shortcut.decrease",false,1,{VK_UP,3}},
    {"pip_resize_taller",L"shortcut.editHeight",L"shortcut.increase",false,1,{VK_DOWN,3}},
    {"pip_resize_narrower_big",L"shortcut.editWidth",L"shortcut.decrease",true,1,{VK_LEFT,7}},
    {"pip_resize_wider_big",L"shortcut.editWidth",L"shortcut.increase",true,1,{VK_RIGHT,7}},
    {"pip_resize_shorter_big",L"shortcut.editHeight",L"shortcut.decrease",true,1,{VK_UP,7}},
    {"pip_resize_taller_big",L"shortcut.editHeight",L"shortcut.increase",true,1,{VK_DOWN,7}},
    {"pip_scale_up",L"shortcut.editScale",L"shortcut.increase",false,1,{VK_UP,2}},
    {"pip_scale_down",L"shortcut.editScale",L"shortcut.decrease",false,1,{VK_DOWN,2}},
    {"pip_scale_up_big",L"shortcut.editScale",L"shortcut.increase",true,1,{VK_UP,6}},
    {"pip_scale_down_big",L"shortcut.editScale",L"shortcut.decrease",true,1,{VK_DOWN,6}},
    {"pip_opacity_down",L"shortcut.editOpacity",L"shortcut.decrease",false,1,{VK_LEFT,2}},
    {"pip_opacity_up",L"shortcut.editOpacity",L"shortcut.increase",false,1,{VK_RIGHT,2}},
    {"pip_opacity_down_big",L"shortcut.editOpacity",L"shortcut.decrease",true,1,{VK_LEFT,6}},
    {"pip_opacity_up_big",L"shortcut.editOpacity",L"shortcut.increase",true,1,{VK_RIGHT,6}},
    {"screenshot",L"shortcut.screenshot",L"",false,0,{'S',1}},
    {"toggle_overlay",L"shortcut.hudOverlay",L"",false,0,{VK_F3,1}},
    {"toggle_fps_details",L"shortcut.fpsDetails",L"",false,0,{VK_TAB,0}},
    {"toggle_settings",L"shortcut.settings",L"",false,0,{VK_F1,0}},
    {"toggle_hdr",L"shortcut.hdr",L"",false,0,{'H',2}},
    {"toggle_hdr_diag",L"shortcut.hdrPatches",L"",false,2,{VK_F4,1}},
    {"toggle_vsync",L"shortcut.vsync",L"",false,0,{'V',2}},
    {"toggle_low_latency",L"shortcut.lowLatency",L"",false,0,{'L',2}},
    {"cycle_aspect",L"shortcut.aspectRatio",L"",false,0,{'A',2}},
    {"cycle_color_range",L"shortcut.colorRange",L"",false,0,{'R',2}},
    {"capture_placeholder_fingerprint",L"shortcut.fingerprint",L"",false,2,{VK_F5,1}},
    {"toggle_levels_diag",L"shortcut.hdrLevels",L"",false,2,{VK_F6,1}},
}};
using ShortcutBindings=std::array<ShortcutChord,ShortcutDefinitions.size()>;
inline constexpr ShortcutBindings DefaultShortcutBindings=[] {
    ShortcutBindings bindings{};
    for(size_t i=0;i<bindings.size();++i)bindings[i]=ShortcutDefinitions[i].defaults;
    return bindings;
}();
// Called at the settings/configuration boundary; runtime dispatch uses typed bindings.
inline constexpr bool ValidShortcutBindings(const ShortcutBindings& bindings) {
    for(size_t i=0;i<bindings.size();++i){
        const auto [key,mods]=bindings[i];
        const bool supported=key==VK_BACK||key==VK_TAB||key==VK_RETURN||key==VK_SPACE||key==VK_PAUSE||key==VK_SNAPSHOT||
            (key>=VK_PRIOR&&key<=VK_DOWN)||key==VK_INSERT||key==VK_DELETE||
            (key>='0'&&key<='9')||(key>='A'&&key<='Z')||(key>=VK_NUMPAD0&&key<=VK_DIVIDE)||
            (key>=VK_F1&&key<=VK_F24)||(key>=VK_OEM_1&&key<=VK_OEM_3)||(key>=VK_OEM_4&&key<=VK_OEM_8)||key==VK_OEM_102;
        if(!supported||mods>7||((mods&2)&&(key==VK_TAB||key==VK_F4))||((mods&3)==3&&key==VK_DELETE))return false;
        for(size_t j=0;j<i;++j)if(bindings[j]==bindings[i])return false;
    }
    return true;
}
static_assert(ValidShortcutBindings(DefaultShortcutBindings));
struct ParsedShortcutBindings { ShortcutBindings bindings=DefaultShortcutBindings;size_t count=0; };
inline std::optional<ParsedShortcutBindings> ParseShortcutBindings(std::string_view text) {
    ParsedShortcutBindings result;std::array<bool,ShortcutDefinitions.size()> seen{};
    while(!text.empty()){
        const auto end=text.find(';');const auto token=text.substr(0,end);
        const auto colon=token.find(':'),comma=token.find(',',colon==token.npos?0:colon+1);
        if(colon==token.npos||comma==token.npos)return std::nullopt;
        size_t index=0;while(index<ShortcutDefinitions.size()&&ShortcutDefinitions[index].id!=token.substr(0,colon))++index;
        if(index==ShortcutDefinitions.size()||seen[index])return std::nullopt;
        auto& chord=result.bindings[index];
        const auto key=std::from_chars(token.data()+colon+1,token.data()+comma,chord.key);
        const auto mods=std::from_chars(token.data()+comma+1,token.data()+token.size(),chord.modifiers);
        if(key.ec!=std::errc{}||key.ptr!=token.data()+comma||mods.ec!=std::errc{}||mods.ptr!=token.data()+token.size())return std::nullopt;
        seen[index]=true;++result.count;
        if(end==text.npos)break;
        if(end+1==text.size())return std::nullopt;
        text.remove_prefix(end+1);
    }
    if(!result.count||!ValidShortcutBindings(result.bindings))return std::nullopt;
    return result;
}
inline std::string SerializeShortcutBindings(const ShortcutBindings& bindings) {
    std::string out;
    for(size_t i=0;i<bindings.size();++i){
        if(i)out+=';';
        out+=ShortcutDefinitions[i].id;out+=':';out+=std::to_string(bindings[i].key);out+=',';out+=std::to_string(bindings[i].modifiers);
    }
    return out;
}
}
