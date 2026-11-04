#pragma once

#include <string_view>
#include "holybear_version.h"
#include "game_identity.h"
#include "update_checker.h"

namespace NitLink::WebViewPolicy {

inline constexpr wchar_t MenuUri[] = L"https://nitlink.invalid/nitlink-menu.html";
inline constexpr wchar_t ContentSecurityPolicy[] =
    L"default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; "
    L"img-src 'self' https://avatars.githubusercontent.com; font-src 'self'; connect-src 'none'; frame-src 'none'; "
    L"worker-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; "
    L"frame-ancestors 'none'";

enum class ResourceKind { Document, Script, Font, Image };
struct Resource {
    std::wstring_view uri;
    const wchar_t* path;
    const wchar_t* contentType;
    ResourceKind kind;
};

// URI paths never become filesystem paths. Only these packaged files are served.
inline constexpr Resource Resources[] = {
    {MenuUri, L"nitlink-menu.html", L"text/html; charset=utf-8", ResourceKind::Document},
    {L"https://nitlink.invalid/locales/en-US.js", L"locales/en-US.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/locales/zh-CN.js", L"locales/zh-CN.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/locales/zh-TW.js", L"locales/zh-TW.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/locales/de-DE.js", L"locales/de-DE.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/locales/ko-KR.js", L"locales/ko-KR.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/locales/ja-JP.js", L"locales/ja-JP.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/menu/menu.js", L"assets/menu/menu.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/menu/updates.js", L"assets/menu/updates.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/menu/vendor/marked.umd.js", L"assets/menu/vendor/marked.umd.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/menu/vendor/purify.min.js", L"assets/menu/vendor/purify.min.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/menu/account-library.js", L"assets/menu/account-library.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/menu/shortcut-editor.js", L"assets/menu/shortcut-editor.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/menu/artwork-editor.js", L"assets/menu/artwork-editor.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/menu/preview-host.js", L"assets/menu/preview-host.js", L"text/javascript; charset=utf-8", ResourceKind::Script},
    {L"https://nitlink.invalid/assets/nitlink.svg",L"assets/nitlink.svg",L"image/svg+xml",ResourceKind::Image},
    {L"https://nitlink.invalid/assets/nitlink-mono.svg",L"assets/nitlink-mono.svg",L"image/svg+xml",ResourceKind::Image},
    {L"https://nitlink.invalid/assets/menu/platforms/nintendo-switch.svg",L"assets/menu/platforms/nintendo-switch.svg",L"image/svg+xml",ResourceKind::Image},
    {L"https://nitlink.invalid/assets/menu/platforms/playstation.svg",L"assets/menu/platforms/playstation.svg",L"image/svg+xml",ResourceKind::Image},
    {L"https://nitlink.invalid/assets/menu/platforms/xbox.svg",L"assets/menu/platforms/xbox.svg",L"image/svg+xml",ResourceKind::Image},
    {L"https://nitlink.invalid/assets/menu/Archivo-400.ttf", L"assets/menu/Archivo-400.ttf", L"font/ttf", ResourceKind::Font},
    {L"https://nitlink.invalid/assets/menu/Archivo-500.ttf", L"assets/menu/Archivo-500.ttf", L"font/ttf", ResourceKind::Font},
    {L"https://nitlink.invalid/assets/menu/Archivo-600.ttf", L"assets/menu/Archivo-600.ttf", L"font/ttf", ResourceKind::Font},
    {L"https://nitlink.invalid/assets/menu/kofi-cup.png", L"assets/menu/kofi-cup.png", L"image/png", ResourceKind::Image},
    {L"https://nitlink.invalid/assets/menu/contributors/holybear.png", L"assets/menu/contributors/holybear.png", L"image/png", ResourceKind::Image},
    {L"https://nitlink.invalid/assets/menu/contributors/cn115.jpg", L"assets/menu/contributors/cn115.jpg", L"image/jpeg", ResourceKind::Image},
    {L"https://nitlink.invalid/assets/menu/contributors/n810k.png", L"assets/menu/contributors/n810k.png", L"image/png", ResourceKind::Image},
};

// The menu has no query or fragment navigation. Exact serialized URLs reject
// sibling documents, alternate authorities, encodings and network file paths.
inline bool IsTrustedDocument(std::wstring_view uri) { return uri == MenuUri; }
inline constexpr std::wstring_view ContributorAvatarUris[] = {
    L"https://avatars.githubusercontent.com/u/285324765?s=160&v=4",
    L"https://avatars.githubusercontent.com/u/50009618?s=160&v=4",
    L"https://avatars.githubusercontent.com/u/68493388?s=160&v=4",
    L"https://avatars.githubusercontent.com/u/7424933?s=160&v=4",
    L"https://avatars.githubusercontent.com/u/82983692?s=160&v=4",
    L"https://avatars.githubusercontent.com/u/50132390?s=160&v=4",
    L"https://avatars.githubusercontent.com/u/177928699?s=160&v=4"
};
inline bool IsContributorAvatarUri(std::wstring_view uri) {
    for(const auto avatar : ContributorAvatarUris)if(uri==avatar)return true;
    return false;
}
inline bool CanReceiveMessage(std::wstring_view source, std::wstring_view current) {
    return IsTrustedDocument(source) && IsTrustedDocument(current);
}
inline const Resource* FindResource(std::wstring_view uri) {
    for (const auto& resource : Resources) if (resource.uri == uri) return &resource;
    constexpr std::wstring_view revision=L"?v=20261102-sharedglass1253-3";
    if(uri.ends_with(revision)){
        uri.remove_suffix(revision.size());
        for(const auto& resource:Resources)if(resource.kind==ResourceKind::Script&&resource.uri==uri)return &resource;
    }
    return nullptr;
}
// Fixed destinations displayed by About and the bundled license documents.
inline constexpr std::wstring_view ExternalLinks[] = {
    L"https://tally.so/r/EkZjNr", L"https://ko-fi.com/klosed89",
    L"https://nitlink.app/", L"https://holybear.tw/", L"https://buymeacoffee.com/holybear",
    L"https://github.com/HolyBearTW/nitlink-holybear-edition/issues",
    L"https://github.com/HolyBearTW", L"https://github.com/CN115", L"https://github.com/n810K",
    L"https://github.com/Noka123", L"https://github.com/i0rph", L"https://github.com/nitlink-dev",
    L"https://github.com/tavricccc", L"https://github.com/nitlink-dev/nitlink",
    L"https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE",
    L"https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md",
    L"https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/ACKNOWLEDGMENTS.md",
    L"https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/docs/4ks-hdr-tonemap.md",
    L"https://github.com/markedjs/marked/tree/v18.1.3",
    L"https://github.com/cure53/DOMPurify/tree/3.4.17",
    L"https://fonts.google.com/specimen/Archivo", L"https://github.com/Omnibus-Type/Archivo",
    L"https://storage.ko-fi.com/cdn/cup-border.png",
    L"https://help.ko-fi.com/hc/en-us/articles/360007169493-Can-I-use-the-Ko-fi-logo-to-promote-my-page",
    L"https://github.com/NVIDIAGameWorks/NVIDIAImageScaling",
    L"https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1",
    L"https://github.com/microsoft/wil", L"https://www.nuget.org/packages/Microsoft.Web.WebView2",
    L"https://aka.ms/webview2-sdk-license", L"https://github.com/elgatosf/capture-device-support",
    L"https://discord.com/developers/docs/topics/rpc", L"https://nsis.sourceforge.io/",
    L"https://github.com/CN115/nitlink-lowlatency/tree/v0.1.2",
    L"https://github.com/CN115/nitlink-lowlatency/tree/6829db62d5a37d7abc05f073065221566267e1ce",
    L"https://github.com/tavricccc/harbor",
    L"https://github.com/tavricccc/harbor/tree/2c243bbbaa1bba4f412b6082964ba03eca64b84a/src/HarborSetup",
    L"https://github.com/primer/octicons", L"https://github.com/primer/octicons/blob/main/icons/mark-github-16.svg",
    L"https://github.com/13bm/elgato4k-linux",
    L"https://github.com/nitlink-dev/nitlink/pull/5", L"https://github.com/nitlink-dev/nitlink/pull/9",
    L"https://github.com/nitlink-dev/nitlink/pull/11", L"https://github.com/nitlink-dev/nitlink/pull/12",
    L"https://github.com/nitlink-dev/nitlink/pull/14", L"https://github.com/nitlink-dev/nitlink/pull/16",
    L"https://github.com/nitlink-dev/nitlink/pull/19", L"https://github.com/nitlink-dev/nitlink/pull/20",
    L"https://github.com/nitlink-dev/nitlink/pull/21", L"https://github.com/nitlink-dev/nitlink/pull/35",
    L"https://github.com/nitlink-dev/nitlink/pull/37"
};
inline bool CanOpenExternal(std::wstring_view current, std::wstring_view target, bool userInitiated) {
    if (!userInitiated || !IsTrustedDocument(current)) return false;
    for (const auto link : ExternalLinks) if (target == link) return true;
    for (const auto& source : HolyBear::AboutDocumentSources) if (target == source.url) return true;
    return HolyBear::IsReleaseUrl(target);
}
inline bool IsLocalScreenshotPath(std::wstring_view path) {
    if (path.size() < 8 || path.size() > 32767 || path[1] != L':' || path[2] != L'\\' ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        !path.ends_with(L".png")) return false;
    for (size_t i = 2; i < path.size(); ++i) {
        if (path[i] < 0x20 || path[i] == L'"' || path[i] == L':' || path[i] == L'/' ||
            path[i] == L'?' || path[i] == L'*') return false;
    }
    return path.find(L"\\..\\") == path.npos && path.find(L"\\.\\") == path.npos;
}
} // namespace NitLink::WebViewPolicy
