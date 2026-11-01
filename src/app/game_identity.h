#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cwctype>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <cmath>

namespace NitLink::Games {
inline constexpr wchar_t WorkerHost[] = L"holybear-game-api.nitlink.workers.dev";
inline constexpr wchar_t ImageOrigin[] = L"https://nitlink.invalid/game-images/";

// Reserved namespace. Built-in asset keys and their old quality profiles keep
// their original IDs; online profiles use sgdb-<canonical decimal ID>.
inline uint32_t ParseId(std::string_view text) {
    uint32_t id = 0;
    if (text.empty() || text.front() == '0' || text.size() > 10) return 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? id : 0;
}
inline uint32_t DynamicId(std::string_view id) {
    return id.starts_with("sgdb-") ? ParseId(id.substr(5)) : 0;
}
inline std::string GameKey(uint32_t id) { return "sgdb-" + std::to_string(id); }
inline bool IsAccountGameKey(std::string_view key) {
    if(key.starts_with("nintendo-HAC-")||key.starts_with("nintendo-BEE-")){
        const auto id=key.substr(13);return id.size()==16&&std::all_of(id.begin(),id.end(),[](char c){return(c>='0'&&c<='9')||(c>='A'&&c<='F');});
    }
    if(key.starts_with("playstation-")){
        const auto id=key.substr(12);return id.size()==12&&(id.starts_with("CUSA")||id.starts_with("PPSA"))&&id[9]=='_'&&
            std::all_of(id.begin()+4,id.begin()+9,[](char c){return c>='0'&&c<='9';})&&id[10]>='0'&&id[10]<='9'&&id[11]>='0'&&id[11]<='9';
    }
    return key.starts_with("xbox-")&&ParseId(key.substr(5))!=0;
}
inline std::wstring BaseGameTitle(std::wstring_view name){
    static const std::wregex edition(LR"([\s:·|—–-]*(?:[（(]\s*)?(?:Nintendo\s+)?Switch\s*2\s*(?:Edition|版|エディション|에디션)\s*[）)]?$)",std::regex_constants::icase);
    return std::regex_replace(std::wstring(name),edition,L"");
}
inline std::wstring CoverSearchTitle(std::wstring_view name){
    static const std::wregex demo(LR"((?:^|[\s:·|—–（(-]+)(?:demo(?:\s*(?:ver\.?|version))?|trial(?:\s*version)?)[\s.）)]*$|[\s:·|—–（(-]*(?:體驗版|体验版|試玩版|试玩版|体験版|체험판)[\s）)]*$)",std::regex_constants::icase);
    return BaseGameTitle(std::regex_replace(BaseGameTitle(name),demo,L""));
}
inline bool MatchesCoverTitle(std::wstring_view product,std::wstring_view game) {
    const auto normalize=[](std::wstring_view value){
        std::wstring key;
        for(auto c:CoverSearchTitle(value))if((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||(c>=0x3040&&c<=0x9fff)||(c>=0xac00&&c<=0xd7af))key+=static_cast<wchar_t>(std::towlower(c));
        return key;
    };
    const auto title=normalize(product);
    return !title.empty()&&title==normalize(game);
}
inline bool IsLocale(std::wstring_view locale) {
    return locale == L"zh-TW" || locale == L"zh-CN" || locale == L"en-US" || locale == L"de-DE" ||
        locale == L"ko-KR" || locale == L"ja-JP";
}
inline bool NeedsLocalizedTitle(std::wstring_view title,std::wstring_view locale) {
    if(locale==L"zh-TW"||locale==L"zh-CN"||locale==L"ja-JP")return std::none_of(title.begin(),title.end(),[](wchar_t c){return c>=0x3040&&c<=0x9fff;});
    if(locale==L"ko-KR")return std::none_of(title.begin(),title.end(),[](wchar_t c){return (c>=0x1100&&c<=0x11ff)||(c>=0x3130&&c<=0x318f)||(c>=0xac00&&c<=0xd7af);});
    return false;
}
inline bool IsCoverLanguage(std::wstring_view language) {
    return IsLocale(language) || language == L"zxx";
}
inline std::wstring LocalizedGameName(std::wstring_view original, std::wstring_view display,
    std::wstring_view locale, std::wstring_view language = {}) {
    // Worker displayName is not necessarily in the requested language. For
    // non-Chinese locales use it only when provenance confirms that locale;
    // the canonical SGDB name is the English fallback.
    const auto matches=[&](std::wstring_view prefix){
        return language==prefix || (language.size()>prefix.size() && language.starts_with(prefix) &&
            (language[prefix.size()]==L'-' || language[prefix.size()]==L'_'));
    };
    if(locale==L"en-US")return std::wstring(!display.empty()&&matches(L"en")?display:original);
    if(locale==L"de-DE"||locale==L"ko-KR"||locale==L"ja-JP"){
        const auto code=locale==L"de-DE"?L"de":locale==L"ko-KR"?L"ko":L"ja";
        return std::wstring(!display.empty()&&matches(code)?display:original);
    }
    if((locale==L"zh-CN"||locale==L"zh-TW")&&!display.empty()&&display.size()<=1024&&matches(L"zh")){
        // Convert only a verified Chinese title, never Japanese kanji or an
        // unlabelled fallback from the Worker. The saved-library map supplies
        // its locale as provenance separately below.
        const auto length=static_cast<int>(display.size());
        const auto flags=locale==L"zh-CN"?LCMAP_SIMPLIFIED_CHINESE:LCMAP_TRADITIONAL_CHINESE;
        const auto* winLocale=locale==L"zh-CN"?L"zh-CN":L"zh-TW";
        const int count=LCMapStringEx(winLocale,flags,display.data(),length,nullptr,0,nullptr,nullptr,0);
        if(count>0){std::wstring converted(count,L'\0');
            if(LCMapStringEx(winLocale,flags,display.data(),length,converted.data(),count,nullptr,nullptr,0)==count)return converted;
        }
    }
    return std::wstring(original);
}
inline bool ValidText(std::wstring_view text, size_t max = 256) {
    if (text.size() > max) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned>(text[i]);
        if (c < 0x20 || c == 0x7f) return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
        } else if (c >= 0xdc00 && c <= 0xdfff) return false;
    }
    return true;
}
// Derived from the native effective source, never from a WebView-supplied URL
// or from the selected game's title. Unknown/PC/Xbox do not imply Nintendo.
inline std::wstring ArtworkPlatformForSource(std::string_view manual, std::wstring_view effective) {
    if (manual=="switch" || manual=="switch2") return L"switch";
    if (manual=="ps5" || manual=="ps4") return L"playstation";
    if (!manual.empty() && manual!="auto" && manual!="other") return L"other";
    std::wstring name;
    for (auto c:effective) {
        if(c==L' ' || c==L'\t')continue;
        name.push_back(c>=L'A'&&c<=L'Z'?c+(L'a'-L'A'):c);
    }
    if(name==L"switch" || name==L"switch2" || name==L"nintendoswitch" || name==L"nintendoswitch2")return L"switch";
    if(name==L"ps5" || name==L"ps5pro" || name==L"ps4" || name==L"playstation5" || name==L"playstation4" || name==L"sonyplaystation5")return L"playstation";
    return name.empty()?L"unknown":L"other";
}
// Exact authority, paths, hex filename and raster extensions. No redirects,
// credentials, ports, fragments, traversal or arbitrary query parameters.
inline bool IsSteamGridCoverUrl(std::wstring_view url) {
    constexpr std::wstring_view origin = L"https://cdn2.steamgriddb.com/";
    if (!url.starts_with(origin)) return false;
    url.remove_prefix(origin.size());
    if (url.starts_with(L"grid/")) url.remove_prefix(5);
    else if (url.starts_with(L"thumb/")) url.remove_prefix(6);
    else return false;
    if (url.ends_with(L"?")) url.remove_suffix(1); // Legacy SGDB URLs.
    if (url.size() < 36 || url.size() > 37) return false;
    for (auto c : url.substr(0, 32))
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    const auto ext = url.substr(32);
    return ext == L".jpg" || ext == L".png" || ext == L".webp";
}
inline bool IsNintendoStoreImageUrl(std::wstring_view url){
    constexpr std::wstring_view prefix=L"https://store.nintendo.com.hk/media/catalog/product/";
    if(url.size()>512||!url.starts_with(prefix))return false;
    url.remove_prefix(prefix.size());
    if(url.empty()||url.find(L"..")!=url.npos||url.find(L"//")!=url.npos)return false;
    if(!std::all_of(url.begin(),url.end(),[](wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'/'||c==L'_'||c==L'-'||c==L'.';}))return false;
    return url.ends_with(L".jpg")||url.ends_with(L".jpeg")||url.ends_with(L".png")||url.ends_with(L".webp");
}
// Nintendo Taiwan's public software directory publishes imageHero assets in
// this Contentful space. Accept only its three-component asset path and the
// exact thumbnail transform used by the directory API.
inline bool IsNintendoTaiwanLineupImageUrl(std::wstring_view url){
    constexpr std::wstring_view prefix=L"https://images.ctfassets.net/o5v89n4kg6h4/";
    if(url.size()>512||!url.starts_with(prefix))return false;
    url.remove_prefix(prefix.size());
    const auto query=url.find(L'?');
    if(query==url.npos)return false;
    const auto params=url.substr(query);
    if(params!=L"?w=400&fm=webp"&&params!=L"?fm=webp&w=400")return false;
    const auto path=url.substr(0,query);
    std::array<std::wstring_view,3> parts{};size_t start=0,index=0;
    while(start<=path.size()&&index<parts.size()){
        const auto slash=path.find(L'/',start);const auto end=slash==path.npos?path.size():slash;
        parts[index++]=path.substr(start,end-start);if(slash==path.npos){start=path.size()+1;break;}start=slash+1;
    }
    if(index!=parts.size()||start<=path.size())return false;
    for(size_t i=0;i<parts.size();++i){
        const auto part=parts[i];if(part.empty()||part.size()>96||part==L"."||part==L"..")return false;
        if(!std::all_of(part.begin(),part.end(),[&](wchar_t c){
            return(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'_'||(i==2&&c==L'.');
        }))return false;
    }
    return parts[2].ends_with(L".jpg")||parts[2].ends_with(L".jpeg")||parts[2].ends_with(L".png")||parts[2].ends_with(L".webp");
}
inline bool IsNintendoArtworkUrl(std::wstring_view url) {
    if(IsNintendoStoreImageUrl(url)||IsNintendoTaiwanLineupImageUrl(url))return true;
    if(url.size()>512)return false;
    constexpr std::wstring_view eshop=L"https://img-eshop.cdn.nintendo.net/i/";
    if(url.starts_with(eshop)){
        url.remove_prefix(eshop.size());
        return url.size()==68&&url.ends_with(L".jpg")&&std::all_of(url.begin(),url.begin()+64,[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});
    }
    if(url.starts_with(L"https://www.nintendo.com/tw/"))url.remove_prefix(std::wstring_view(L"https://www.nintendo.com").size());
    else if(url.starts_with(L"https://www.nintendo.com.tw/"))url.remove_prefix(std::wstring_view(L"https://www.nintendo.com.tw").size());
    else return false;
    if(url.empty()||url.front()!=L'/'||url.find(L"..")!=url.npos||url.find(L"//")!=url.npos)return false;
    if(!std::all_of(url.begin(),url.end(),[](wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'/'||c==L'_'||c==L'-'||c==L'.';}))return false;
    return url.ends_with(L".png")||url.ends_with(L".jpg")||url.ends_with(L".webp");
}
inline bool IsLegacyNintendoTaiwanArtworkUrl(std::wstring_view url) {
    constexpr std::wstring_view twOrigin=L"https://www.nintendo.com/tw/";
    constexpr std::wstring_view twCompatOrigin=L"https://www.nintendo.com.tw/";
    return (url.starts_with(twOrigin)||url.starts_with(twCompatOrigin))&&IsNintendoArtworkUrl(url);
}
inline bool IsNintendoEshopArtworkUrl(std::wstring_view url) {
    constexpr std::wstring_view origin=L"https://img-eshop.cdn.nintendo.net/i/";
    return url.starts_with(origin)&&IsNintendoArtworkUrl(url);
}
inline bool IsPlayStationArtworkUrl(std::wstring_view url) {
    constexpr std::wstring_view origin=L"https://image.api.playstation.com/vulcan/";
    if(url.size()>512 || !url.starts_with(origin))return false;
    url.remove_prefix(origin.size());
    if(url.empty() || url.find(L"..")!=url.npos || url.find(L"//")!=url.npos)return false;
    if(!std::all_of(url.begin(),url.end(),[](wchar_t c){return(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'/'||c==L'-'||c==L'_'||c==L'.';}))return false;
    return url.ends_with(L".png")||url.ends_with(L".jpg")||url.ends_with(L".webp");
}
inline bool IsAccountArtworkUrl(std::wstring_view url) {
    if(url.size()>2048)return false;
    for(const auto prefix:{std::wstring_view(L"https://atum-img-lp1.cdn.nintendo.net/i/c/"),
        std::wstring_view(L"https://image.api.playstation.com/vulcan/"),std::wstring_view(L"https://image.api.playstation.com/cdn/"),
        std::wstring_view(L"https://store-images.s-microsoft.com/image/"),std::wstring_view(L"https://images-eds-ssl.xboxlive.com/image/"),
        std::wstring_view(L"https://images-eds.xboxlive.com/image/"),
        std::wstring_view(L"https://images-eds.xboxlive.com/image?"),std::wstring_view(L"https://images-eds-ssl.xboxlive.com/image?")}) {
        if(!url.starts_with(prefix))continue;const auto path=url.substr(prefix.size());
        return !path.empty()&&path.find(L"..") == path.npos&&path.find(L"//")==path.npos&&std::all_of(path.begin(),path.end(),[](wchar_t c){
            return(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||std::wstring_view(L"/_.-?=&%+").find(c)!=std::wstring_view::npos;
        });
    }
    return false;
}
inline bool IsCoverUrl(std::wstring_view url) { return IsSteamGridCoverUrl(url)||IsNintendoArtworkUrl(url)||IsPlayStationArtworkUrl(url)||IsAccountArtworkUrl(url); }
struct ArtworkMetadata {
    std::string accountGameKey;
    std::wstring provider=L"steamgriddb", sourceLabel=L"SteamGridDB", language, region, kind=L"cover";
    bool isOfficial=false;
};
inline bool IsImageResource(std::wstring_view uri) {
    if (!uri.starts_with(ImageOrigin)) return false;
    uri.remove_prefix(std::wstring_view(ImageOrigin).size());
    if (!uri.ends_with(L".png")) return false;
    uri.remove_suffix(4);
    return !uri.empty() && uri.size() <= 20 &&
        std::all_of(uri.begin(), uri.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}
struct SavedGame : ArtworkMetadata {
    std::string key; // The persistent selection key, independent of artwork catalogue IDs.
    uint32_t id = 0;
    std::wstring name;
    std::map<std::wstring, std::wstring> displayNames;
    std::wstring coverUrl;
    std::wstring coverThumb;
    std::wstring coverShape;
    uint32_t coverId = 0;
    bool manualCover = false;
    bool favorite = false;
    uint64_t lastUsedUtc = 0;
    std::wstring customName;
    std::wstring artworkMode=L"original",finalImageKey,sourceImageKey;
    double cropX=.5,cropY=.5,artworkZoom=1;
    std::wstring DisplayName(std::wstring_view locale) const {
        if (!customName.empty()) return customName;
        const auto it = displayNames.find(std::wstring(locale));
        if(it==displayNames.end())return name;
        if((locale==L"en-US"||locale==L"de-DE")&&std::any_of(it->second.begin(),it->second.end(),[](wchar_t c){
            return (c>=0x3400&&c<=0x9fff)||(c>=0xf900&&c<=0xfaff);
        }))return name;
        return LocalizedGameName(name,it->second,locale,locale);
    }
};
inline bool ValidSavedGame(std::string_view key, const SavedGame& game) {
    const bool account=IsAccountGameKey(key);
    const bool builtin = game.id == 0 && !key.empty() && key.size() <= 64 && !key.starts_with("sgdb-") &&
        std::all_of(key.begin(),key.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';});
    if ((!builtin && !account && (!game.id || DynamicId(key) != game.id)) || game.name.empty() || !ValidText(game.name) ||
        !ValidText(game.customName) || game.lastUsedUtc > 253402300799ull ||
        game.displayNames.size() > 6 || (!game.coverUrl.empty() && !IsCoverUrl(game.coverUrl)) ||
        (!game.coverThumb.empty() && !IsCoverUrl(game.coverThumb))) return false;
    if(!game.coverShape.empty()&&game.coverShape!=L"square"&&game.coverShape!=L"portrait"&&game.coverShape!=L"landscape")return false;
    if(!ValidText(game.sourceLabel,128)||(!game.language.empty()&&!IsCoverLanguage(game.language))||
       (!game.region.empty()&&(game.region.size()!=2||!std::all_of(game.region.begin(),game.region.end(),[](wchar_t c){return c>=L'A'&&c<=L'Z';}))))return false;
    if(game.provider==L"nintendo-account"||game.provider==L"playstation-account"||game.provider==L"xbox-account"){
        const std::string_view origin=game.accountGameKey.empty()?key:std::string_view(game.accountGameKey);
        if(!IsAccountGameKey(origin))return false;
        const auto source=origin.substr(0,origin.find('-'));
        if(game.provider!=std::wstring(source.begin(),source.end())+L"-account"||!game.isOfficial||
           (!game.coverUrl.empty()&&!IsAccountArtworkUrl(game.coverUrl))||(!game.coverThumb.empty()&&!IsAccountArtworkUrl(game.coverThumb)))return false;
    }else if(IsNintendoArtworkUrl(game.coverUrl)){
        const bool tw=game.provider==L"nintendo-tw"&&game.region==L"TW"&&game.sourceLabel==L"Official Nintendo Taiwan";
        const bool hk=game.provider==L"nintendo-hk-eshop"&&game.region==L"HK"&&game.sourceLabel==L"Official Nintendo eShop CDN";
        const bool store=game.provider==L"nintendo-hk-store"&&game.region==L"HK"&&game.sourceLabel==L"Official Nintendo Hong Kong Store"&&IsNintendoStoreImageUrl(game.coverUrl);
        if(IsNintendoStoreImageUrl(game.coverUrl)&&(!store||(!game.coverThumb.empty()&&!IsNintendoStoreImageUrl(game.coverThumb))))return false;
        if(IsNintendoTaiwanLineupImageUrl(game.coverUrl)&&(!tw||(!game.coverThumb.empty()&&!IsNintendoTaiwanLineupImageUrl(game.coverThumb))||
           (game.kind!=L"logo"&&game.kind!=L"keyArt")||game.coverShape!=L"landscape"))return false;
        if(tw&&!IsNintendoTaiwanLineupImageUrl(game.coverUrl)&&(!IsLegacyNintendoTaiwanArtworkUrl(game.coverUrl)||(!game.coverThumb.empty()&&!IsLegacyNintendoTaiwanArtworkUrl(game.coverThumb))))return false;
        if(hk&&(!IsNintendoEshopArtworkUrl(game.coverUrl)||(!game.coverThumb.empty()&&!IsNintendoEshopArtworkUrl(game.coverThumb))))return false;
        if((!tw&&!hk&&!store)||!game.isOfficial||(!game.language.empty()&&!IsCoverLanguage(game.language))||
           (game.kind!=L"boxArt"&&game.kind!=L"logo"&&game.kind!=L"keyArt"&&game.kind!=L"squareIcon"&&!(store&&game.kind==L"cover"))||
           (!game.coverThumb.empty()&&!IsNintendoArtworkUrl(game.coverThumb)))return false;
    }else if(IsPlayStationArtworkUrl(game.coverUrl)){
        if(game.provider!=L"playstation-tw" || game.sourceLabel!=L"Official PlayStation Taiwan" ||
           game.region!=L"TW" || !game.isOfficial ||
           (game.kind!=L"squareIcon"&&game.kind!=L"boxArt"&&game.kind!=L"keyArt"&&game.kind!=L"logo") ||
           (!game.coverThumb.empty()&&!IsPlayStationArtworkUrl(game.coverThumb)))return false;
    }else if(game.provider!=L"steamgriddb"||game.isOfficial||(game.kind!=L"cover"&&game.kind!=L"boxArt"))return false;
    for (const auto& [locale, name] : game.displayNames)
        if (!IsLocale(locale) || !ValidText(name)) return false;
    return (game.artworkMode==L"original"||game.artworkMode==L"fit"||game.artworkMode==L"crop")&&
        std::isfinite(game.cropX)&&std::isfinite(game.cropY)&&std::isfinite(game.artworkZoom)&&
        game.cropX>=0&&game.cropX<=1&&game.cropY>=0&&game.cropY<=1&&game.artworkZoom>=1&&game.artworkZoom<=3&&
        (game.finalImageKey.empty()||(game.finalImageKey.size()==64&&std::all_of(game.finalImageKey.begin(),game.finalImageKey.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})))&&
        (game.sourceImageKey.empty()||(game.sourceImageKey.size()==64&&std::all_of(game.sourceImageKey.begin(),game.sourceImageKey.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})));
}
} // namespace NitLink::Games
