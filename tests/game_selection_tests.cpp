#include "app/config.h"
#include "app/settings_message.h"
#include "app/game_service.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {
unsigned failures = 0;
void Check(bool pass, const char* name) {
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
struct SlowProvider final : NitLink::Games::Provider {
    std::atomic<bool>& started;
    explicit SlowProvider(std::atomic<bool>& started) : started(started) {}
    NitLink::Games::Reply Search(std::wstring_view query, std::wstring_view, const NitLink::Games::Cancelled& cancelled) override {
        if (query == L"slow") {
            started = true;
            while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        NitLink::Games::Reply reply;
        reply.status = L"ok";
        reply.games.push_back({query == L"slow" ? 10u : 20u, 0, L"Original", L"顯示名稱", L"fixture"});
        return reply;
    }
    NitLink::Games::Reply Covers(uint32_t, std::wstring_view, std::wstring_view, uint32_t, const NitLink::Games::Cancelled&) override { return {}; }
};
struct GalleryProvider final : NitLink::Games::Provider {
    std::vector<NitLink::Games::Cover> covers;
    NitLink::Games::Reply Search(std::wstring_view,std::wstring_view,const NitLink::Games::Cancelled&) override {return {};}
    NitLink::Games::Reply Covers(uint32_t,std::wstring_view,std::wstring_view,uint32_t,const NitLink::Games::Cancelled&) override {
        NitLink::Games::Reply reply;reply.status=L"ok";reply.covers=covers;return reply;
    }
};
struct OfficialThumbnailProvider final : NitLink::Games::Provider {
    NitLink::Games::Game game;
    std::atomic<unsigned> communityRequests{0};
    NitLink::Games::Reply Search(std::wstring_view,std::wstring_view,const NitLink::Games::Cancelled&) override {
        NitLink::Games::Reply reply;reply.status=L"ok";reply.games={game};return reply;
    }
    NitLink::Games::Reply Covers(uint32_t,std::wstring_view,std::wstring_view,uint32_t,const NitLink::Games::Cancelled&) override {
        ++communityRequests;return {};
    }
};
}
int main() {
    using namespace NitLink;
    using namespace Games;
    Check(MatchesCoverTitle(L"mario kart world",L"Mario Kart World")&&
          MatchesCoverTitle(L"冒險遊戲 Nintendo Switch 2 Edition",L"冒險遊戲")&&
          MatchesCoverTitle(L"Sonic Racing: CrossWorlds",L"Sonic Racing: CrossWorlds - Demo")&&
          !MatchesCoverTitle(L"東方 New World",L"Mario Kart World")&&
          !MatchesCoverTitle(L"Another Game Deluxe Edition",L"Selected Game Deluxe Edition")&&
          !MatchesCoverTitle(L"Adventure 2",L"Adventure")&&!MatchesCoverTitle(L"Adventure",L"Adventure Returns"),"official products require the complete normalized title, never a shared word, edition token or sequel prefix");
    Check(NeedsLocalizedTitle(L"Mario Kart World",L"zh-TW")&&!NeedsLocalizedTitle(L"瑪利歐賽車世界",L"zh-TW")&&
          !NeedsLocalizedTitle(L"マリオカート ワールド",L"ja-JP")&&!NeedsLocalizedTitle(L"마리오 카트 월드",L"ko-KR")&&
          !NeedsLocalizedTitle(L"Mario Kart World",L"de-DE"),"an English platform fallback does not suppress missing Chinese/Japanese/Korean catalogue labels");
    Check(CoverSearchTitle(L"Sonic Racing: CrossWorlds - Demo")==L"Sonic Racing: CrossWorlds"&&
          CoverSearchTitle(L"Majogami Demo ver.")==L"Majogami"&&
          CoverSearchTitle(L"STREET FIGHTER 6 DEMO")==L"STREET FIGHTER 6"&&
          CoverSearchTitle(L"魔女祇兵 體驗版")==L"魔女祇兵"&&
          CoverSearchTitle(L"DEMON X MACHINA")==L"DEMON X MACHINA","cover lookup strips demo suffixes without changing unrelated names");
    Check(DynamicId("sgdb-5303117") == 5303117 && !DynamicId("sgdb-01") && !DynamicId("sgdb-0") &&
          !DynamicId("sgdb-4294967296") && !DynamicId("spider-man-2"), "canonical dynamic IDs");
    const std::wstring url = L"https://cdn2.steamgriddb.com/grid/72654158f52bf25f9a8101f78aceaa2f.jpg";
    Check(IsCoverUrl(url) && IsCoverUrl(url + L"?"), "known CDN cover formats");
    Check(!IsCoverUrl(L"https://cdn2.steamgriddb.com.evil.test/grid/72654158f52bf25f9a8101f78aceaa2f.jpg") &&
          !IsCoverUrl(url + L"#x") && !IsCoverUrl(url + L"?redirect=evil") &&
          !IsCoverUrl(L"file:///C:/secret.png"), "cover authority/path validation");
    const std::wstring official=L"https://www.nintendo.com/tw/zelda/totk/products/img/soft/img__package.png";
    Check(IsNintendoArtworkUrl(official)&&!IsSteamGridCoverUrl(official)&&
          !IsNintendoArtworkUrl(official+L"?redirect=evil")&&
          !IsNintendoArtworkUrl(L"https://www.nintendo.com.evil.test/tw/cover.png")&&
          !IsNintendoArtworkUrl(L"https://www.nintendo.com:443/tw/cover.png")&&
          !IsNintendoArtworkUrl(L"https://www.nintendo.com/tw/../cover.png")&&
          !IsNintendoArtworkUrl(L"https://www.nintendo.com/tw/%2e%2e/cover.png"),"official artwork authority and path boundary");
    Check(IsLocale(L"de-DE") && IsCoverLanguage(L"de-DE") && !IsLocale(L"fr-FR"),
          "German search, cover, and saved-title locale is accepted");
    Check(IsLocale(L"ko-KR")&&IsLocale(L"ja-JP")&&IsCoverLanguage(L"ko-KR")&&IsCoverLanguage(L"ja-JP"),
          "Korean and Japanese search, cover and saved-title locales are accepted");
    Check(LocalizedGameName(L"The Legend of Zelda",L"薩爾達傳說",L"en-US",L"zh-hant")==L"The Legend of Zelda"&&
          LocalizedGameName(L"The Legend of Zelda",L"薩爾達傳說",L"de-DE",L"zh-hant")==L"The Legend of Zelda",
          "English and German never use an upstream Traditional-Chinese fallback");
    Check(LocalizedGameName(L"The Legend of Zelda",L"塞尔达传说",L"zh-CN",L"zh-hans")==L"塞尔达传说"&&
          LocalizedGameName(L"The Legend of Zelda",L"薩爾達傳說",L"zh-CN",L"zh-hant")==L"萨尔达传说"&&
          LocalizedGameName(L"Zelda",L"薩爾達 🎮",L"zh-CN",L"zh-hant")==L"萨尔达 🎮",
          "Simplified-Chinese API titles survive and legacy Traditional text is converted generically");
    Check(LocalizedGameName(L"The Legend of Zelda",L"塞尔达传说",L"zh-TW",L"zh-hans")==L"塞爾達傳說"&&
          LocalizedGameName(L"The Legend of Zelda",L"ゼルダの伝説",L"zh-TW",L"ja")==L"The Legend of Zelda"&&
          LocalizedGameName(L"The Legend of Zelda",L"薩爾達傳說",L"zh-CN")==L"The Legend of Zelda",
          "Chinese locales convert verified script and reject unrelated or unlabelled titles");
    Check(LocalizedGameName(L"The Legend of Zelda",L"Die Legende von Zelda",L"de-DE",L"de")==L"Die Legende von Zelda",
          "verified German title retains its locale");
    Check(LocalizedGameName(L"Zelda",L"The Legend of Zelda",L"en-US",L"en")==L"The Legend of Zelda",
          "verified English title retains its locale");
    Check(LocalizedGameName(L"The Legend of Zelda",L"薩爾達傳說",L"zh-TW",L"zh-hant")==L"薩爾達傳說",
          "verified Traditional-Chinese title retains its locale");
    Check(LocalizedGameName(L"The Legend of Zelda",L"ゼルダの伝説",L"ja-JP",L"ja")==L"ゼルダの伝説"&&
          LocalizedGameName(L"The Legend of Zelda",L"젤다의 전설",L"ko-KR",L"ko-KR")==L"젤다의 전설"&&
          LocalizedGameName(L"The Legend of Zelda",L"薩爾達傳說",L"ja-JP",L"zh-hant")==L"The Legend of Zelda"&&
          LocalizedGameName(L"The Legend of Zelda",L"薩爾達傳說",L"ko-KR",L"zh-hant")==L"The Legend of Zelda"&&
          LocalizedGameName(L"The Legend of Zelda",L"薩爾達傳說",L"ja-JP")==L"The Legend of Zelda"&&
          LocalizedGameName(L"The Legend of Zelda",L"젤다의 전설",L"ja-JP",L"ko")==L"The Legend of Zelda",
          "Japanese and Korean require matching title provenance and otherwise use English");
    SavedGame oldNames;oldNames.name=L"The Legend of Zelda";
    oldNames.displayNames={{L"en-US",L"薩爾達傳說"},{L"de-DE",L"薩爾達傳說"},{L"zh-CN",L"薩爾達傳說"}};
    Check(oldNames.DisplayName(L"en-US")==oldNames.name&&oldNames.DisplayName(L"de-DE")==oldNames.name&&
          oldNames.DisplayName(L"zh-CN")==L"萨尔达传说","previously polluted library names recover without deleting saved games");
    oldNames.customName=L"我的ゼルダ 🎮";
    Check(oldNames.DisplayName(L"en-US")==oldNames.customName&&oldNames.DisplayName(L"zh-CN")==oldNames.customName,
          "user-defined Unicode names bypass automatic locale fallback");
    Cover taiwanLandscape;taiwanLandscape.provider=L"nintendo-tw";taiwanLandscape.sourceLabel=L"Official Nintendo Taiwan";
    taiwanLandscape.region=L"TW";taiwanLandscape.isOfficial=true;taiwanLandscape.shape=L"landscape";taiwanLandscape.kind=L"keyArt";
    Cover taiwanPortrait=taiwanLandscape;taiwanPortrait.shape=L"portrait";
    Cover taiwanLogo=taiwanLandscape;taiwanLogo.kind=L"logo";
    Cover hongKongLandscape=taiwanLandscape;hongKongLandscape.provider=L"nintendo-hk-eshop";
    hongKongLandscape.sourceLabel=L"Official Nintendo eShop CDN";hongKongLandscape.region=L"HK";
    const std::vector<Cover> searchArt={taiwanPortrait,taiwanLogo,hongKongLandscape,taiwanLandscape};
    Check(PreferredTaiwanLandscapeArtwork(searchArt)==&searchArt[1]&&
          PreferredTaiwanLandscapeArtwork({taiwanLandscape})!=nullptr,
          "search thumbnails prefer verified Nintendo Taiwan landscape artwork, including logo covers");
    Check(PreferredActivityArtwork(searchArt,L"zh-TW")==&searchArt[0],
          "official portrait precedes official landscape when no square is available");
    Check(PreferredTaiwanLandscapeArtwork({taiwanPortrait,hongKongLandscape})==nullptr,
          "portrait and non-Taiwan art do not replace the existing search thumbnail");
    Cover communitySquare;communitySquare.id=81;communitySquare.url=communitySquare.thumb=url;communitySquare.shape=L"square";
    Cover officialPortrait=taiwanPortrait;officialPortrait.id=82;officialPortrait.url=officialPortrait.thumb=official;
    std::vector<Cover> loaded={communitySquare,officialPortrait};
    SavedGame automatic;
    Check(!AutomaticActivityArtwork(automatic,{communitySquare},false,L"zh-TW")&&!AutomaticActivityArtwork(automatic,loaded,false,L"zh-TW"),
          "no default is selected before the entire image batch settles");
    Check(AutomaticActivityArtwork(automatic,loaded,true,L"zh-TW")==&loaded[0],
          "a community square precedes an official portrait");
    std::reverse(loaded.begin(),loaded.end());
    Check(AutomaticActivityArtwork(automatic,loaded,true,L"zh-TW")==&loaded[1],
          "selection priority is independent of image arrival order");
    const std::vector<Cover> noOfficial={communitySquare};
    Check(AutomaticActivityArtwork(automatic,noOfficial,true,L"zh-TW")==&noOfficial[0]&&
          !AutomaticActivityArtwork(automatic,{},true,L"zh-TW"),
          "square is the fallback when no official image loaded; all failures select nothing");
    Cover officialSquare=officialPortrait;officialSquare.shape=L"square";
    auto localizedPortrait=officialPortrait;localizedPortrait.language=L"zh-TW";
    auto englishSquare=officialSquare;englishSquare.language=L"en-US";
    const std::vector<Cover> localeChoices={englishSquare,localizedPortrait};
    Check(PreferredActivityArtwork(localeChoices,L"zh-TW")==&localeChoices[1]&&PreferredActivityArtwork(localeChoices,L"en-US")==&localeChoices[0],"official candidates retain every locale while the effective UI language chooses the default");
    Cover communityPortrait=communitySquare;communityPortrait.shape=L"portrait";
    Cover communityLandscape=communitySquare;communityLandscape.shape=L"landscape";
    const std::vector<Cover> priority={officialSquare,communitySquare,officialPortrait,taiwanLandscape,communityPortrait,communityLandscape};
    for(const auto* locale:{L"zh-TW",L"zh-CN",L"en-US",L"de-DE",L"ja-JP",L"ko-KR"}){
        for(size_t index=0;index<priority.size();++index){
            Check(ActivityArtworkPriority(priority[index],locale)==static_cast<int>(index),"six source/shape tiers apply in every locale");
            const std::vector<Cover> remaining(priority.begin()+index,priority.end());
            Check(PreferredActivityArtwork(remaining,locale)==&remaining.front(),"each missing tier falls back to the next available shape/source");
        }
        Check(!AutomaticActivityArtwork(automatic,loaded,false,locale)&&AutomaticActivityArtwork(automatic,loaded,true,locale)==&loaded[1],"selection waits for the complete image batch in every locale");
        auto manual=automatic;SelectArtwork(manual,officialPortrait);manual.manualCover=true;
        Check(!AutomaticActivityArtwork(manual,priority,true,locale),"manual artwork always survives automatic refresh");
    }
    automatic.manualCover=true;
    Check(!AutomaticActivityArtwork(automatic,loaded,true,L"zh-TW"),"batch completion never overrides a manual cover");
    automatic.manualCover=false;automatic.finalImageKey=std::wstring(64,L'a');
    Check(!AutomaticActivityArtwork(automatic,loaded,true,L"zh-TW"),"batch completion preserves an existing custom edit");
    Check(IsAccountGameKey("nintendo-BEE-0400000000000001")&&IsAccountGameKey("playstation-PPSA21568_00")&&IsAccountGameKey("xbox-2014350744")&&!IsAccountGameKey("nintendo-HAC-bad"),"platform IDs have separate persistent namespaces");
    SavedGame linked;linked.id=5483943;linked.name=L"Example";linked.accountGameKey="xbox-2014350744";
    linked.provider=L"xbox-account";linked.isOfficial=true;linked.coverShape=L"square";
    linked.coverUrl=linked.coverThumb=L"https://store-images.s-microsoft.com/image/apps.1.test";
    Check(ValidSavedGame("sgdb-5483943",linked),"linked artwork does not replace the selected catalogue game's identity");
    linked.accountGameKey="playstation-PPSA21568_00";
    Check(!ValidSavedGame("sgdb-5483943",linked),"account artwork provenance must match its provider");
    Check(ParseSettingsMessage(LR"({"action":"applyAutoGameCover","value":{"gameId":5303117,"request":7,"coverId":82}})").has_value(),
          "image readiness acknowledgment carries only IDs");
    for(const auto* invalid:{LR"({"action":"applyAutoGameCover","value":{"gameId":5303117,"request":0,"coverId":82}})",
        LR"({"action":"applyAutoGameCover","value":{"gameId":5303117,"request":1.5,"coverId":82}})",
        LR"({"action":"applyAutoGameCover","value":{"gameId":5303117,"request":7,"coverId":82,"url":"https://evil.test/x"}})"})
        Check(!ParseSettingsMessage(invalid),"readiness cannot inject a URL or malformed request ID");
    Check(IsImageResource(L"https://nitlink.invalid/game-images/1.png") &&
          !IsImageResource(L"https://nitlink.invalid/game-images/../secret.png"), "opaque local image resources");
    const std::wstring store=L"https://store.nintendo.com.hk/media/catalog/product/cache/55351146f864384e20b16a2043f3a323/1/1/110.jpg";
    Check(IsCoverUrl(store)&&!IsCoverUrl(store+L"?redirect=evil")&&
          !IsCoverUrl(L"https://store.nintendo.com.hk.evil.test/media/catalog/product/x.jpg")&&
          !IsCoverUrl(L"https://store.nintendo.com.hk/media/catalog/product/../x.jpg")&&
          !IsCoverUrl(L"https://store.nintendo.com.hk:443/media/catalog/product/x.jpg")&&
          !IsCoverUrl(L"https://store.nintendo.com.hk/media/catalog/product/%2e%2e/x.jpg"),"Hong Kong store image boundary");
    SavedGame storeGame;storeGame.id=35744;storeGame.name=L"The Legend of Zelda: Breath of the Wild";
    storeGame.coverUrl=storeGame.coverThumb=store;storeGame.provider=L"nintendo-hk-store";storeGame.region=L"HK";
    storeGame.sourceLabel=L"Official Nintendo Hong Kong Store";storeGame.isOfficial=true;
    Check(ValidSavedGame("sgdb-35744",storeGame),"store provenance survives native config validation");
    SavedGame localizedGame;localizedGame.id=35744;localizedGame.name=L"Breath of the Wild";
    localizedGame.displayNames={{L"en-US",L"Breath of the Wild"},{L"zh-TW",L"曠野之息"},
        {L"zh-CN",L"旷野之息"},{L"de-DE",L"Breath of the Wild"},
        {L"ja-JP",L"ゼルダの伝説 ブレス オブ ザ ワイルド"},{L"ko-KR",L"젤다의 전설 브레스 오브 더 와일드"}};
    Check(ValidSavedGame("sgdb-35744",localizedGame),"saved games retain all six display locales");
    storeGame.provider=L"nintendo-tw";storeGame.region=L"TW";storeGame.sourceLabel=L"Official Nintendo Taiwan";
    Check(!ValidSavedGame("sgdb-35744",storeGame),"store image cannot impersonate Taiwan provenance");
    Check(ParseSettingsMessage(LR"({"action":"getGameCovers","value":{"gameId":35744,"shape":"all","page":1,"request":9,"provider":"nintendo","officialQuery":""}})").has_value(),"generic official catalogue browse schema");
    Check(!ParseSettingsMessage(LR"({"action":"getGameCovers","value":{"gameId":35744,"shape":"all","page":1,"request":9,"provider":"https://evil.test","officialQuery":""}})"),"renderer cannot choose network provider URL");
    Check(ParseSettingsMessage(LR"({"action":"setGame","value":"sgdb-5303117"})").has_value(), "dynamic game schema");
    Check(!ParseSettingsMessage(LR"({"action":"setGame","value":"sgdb-01"})"), "noncanonical ID rejected");
    Check(ParseSettingsMessage(LR"({"action":"searchGames","value":{"query":"薩爾達 / ゼルダ","request":7}})").has_value(), "Unicode query schema");
    Check(!ParseSettingsMessage(LR"({"action":"searchGames","value":{"query":"bad\ud800","request":7}})"), "malformed Unicode rejected");
    Check(!ParseSettingsMessage(LR"({"action":"selectGameCover","value":{"gameId":1,"coverId":2,"url":"https://evil.test/x"}})"), "renderer cannot supply cover URLs");
    wchar_t temp[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp)) return 1;
    const auto path = std::filesystem::path(temp) / (L"nitlink-game-selection-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    Config saved;
    saved.currentGameId = "sgdb-5303117";
    auto& game = saved.onlineGames[saved.currentGameId];
    game.id = 5303117; game.name = L"The Legend of Zelda: Tears of the Kingdom";
    game.displayNames[L"zh-TW"] = L"薩爾達傳說 王國之淚";
    game.displayNames[L"zh-CN"] = L"塞尔达传说 王国之泪";
    game.coverUrl = url; game.coverThumb = url; game.coverId = 694138; game.manualCover = true;
    saved.gameSettings["spider-man-2"].nisEnabled = true;
    saved.gameSettings[saved.currentGameId].colorExpansion = false;
    auto& oldAccount=saved.onlineGames["nintendo-BEE-0400000000000001"];
    oldAccount.key="nintendo-BEE-0400000000000001";oldAccount.name=L"Mario Kart World";
    oldAccount.displayNames={{L"zh-TW",L"Mario Kart World"},{L"ja-JP",L"マリオカート ワールド"}};
    Check(saved.Save(path.string()), "persist online game");
    Config restored;
    Check(restored.Load(path.string()), "restore online game");
    Check(!restored.onlineGames.at(oldAccount.key).displayNames.contains(L"zh-TW")&&restored.onlineGames.at(oldAccount.key).DisplayName(L"ja-JP")==L"マリオカート ワールド","old mislabeled English account names no longer block catalogue localization after settings reload");
    const auto found = restored.onlineGames.find(saved.currentGameId);
    Check(found != restored.onlineGames.end() && found->second.manualCover && found->second.coverUrl == url &&
          found->second.coverId == 694138 && found->second.DisplayName(L"zh-TW") == game.displayNames[L"zh-TW"], "manual art and locale roundtrip");
    Check(restored.gameSettings["spider-man-2"].nisEnabled && !restored.gameSettings[saved.currentGameId].colorExpansion,
          "old and dynamic quality profiles coexist");
    Config cleared=restored;
    cleared.onlineGames.at(saved.currentGameId).favorite=true;
    cleared.ClearCurrentGame();
    Check(cleared.currentGameId.empty()&&!cleared.onlineGames.contains(saved.currentGameId)&&
          cleared.onlineGames.contains(oldAccount.key)&&cleared.gameSettings.contains(saved.currentGameId),
          "clearing a local game removes its favorite/library record while retaining linked games and saved quality profiles");
    Check(cleared.Save(path.string()),"persist the cleared local library");
    Config clearReload;Check(clearReload.Load(path.string())&&clearReload.currentGameId.empty()&&
          !clearReload.onlineGames.contains(saved.currentGameId)&&clearReload.onlineGames.contains(oldAccount.key),
          "a cleared local game stays removed after restarting");
    cleared.currentGameId=oldAccount.key;cleared.ClearCurrentGame();
    Check(cleared.currentGameId.empty()&&cleared.onlineGames.contains(oldAccount.key),
          "clearing a linked-platform game deselects it without deleting account library data");
    cleared.currentGameId="spider-man-2";cleared.onlineGames[cleared.currentGameId].name=L"Marvel's Spider-Man 2";
    cleared.ClearCurrentGame();
    Check(!cleared.onlineGames.contains("spider-man-2")&&cleared.gameSettings["spider-man-2"].nisEnabled,
          "built-in local games are also removed without erasing their saved video settings");
    Config reset;
    reset.currentGameId = "sgdb-5303117";
    reset.onlineGames[reset.currentGameId] = game;
    reset.gameSettings[reset.currentGameId] = {true, true};
    reset.nisEnabled = true;
    reset.colorExpansion = true;
    reset.audioFifoMs = 12;
    reset.manualHdmiSource = "ps5";
    reset.ResetGamePageSettings();
    Check(reset.currentGameId.empty() && reset.onlineGames.empty() && reset.gameSettings.empty() &&
          reset.nisEnabled && reset.colorExpansion, "game-page reset clears saved game profiles without changing live video controls");
    Check(reset.audioFifoMs == 12 && reset.manualHdmiSource == "ps5",
          "game-page reset preserves audio and HDMI settings");
    auto& officialGame=restored.onlineGames[saved.currentGameId];officialGame.coverUrl=official;officialGame.coverThumb=official;
    officialGame.coverId=4200000002u;officialGame.provider=L"nintendo-tw";officialGame.sourceLabel=L"Official Nintendo Taiwan";
    officialGame.language=L"zh-TW";officialGame.region=L"TW";officialGame.kind=L"boxArt";officialGame.isOfficial=true;officialGame.favorite=true;
    Check(restored.Save(path.string()),"save manually selected official artwork");
    Config officialRestored;Check(officialRestored.Load(path.string()),"load official artwork without searching");
    const auto& officialRecord=officialRestored.onlineGames.at(saved.currentGameId);
    Check(officialRecord.coverUrl==official&&officialRecord.provider==L"nintendo-tw"&&officialRecord.isOfficial&&
          officialRecord.language==L"zh-TW"&&officialRecord.region==L"TW"&&officialRecord.manualCover&&officialRecord.favorite,
          "official source metadata and manual choice survive restart");
    officialRestored.onlineGames.at(saved.currentGameId).isOfficial=false;
    Check(!officialRestored.Save(path.string()),"official URL cannot masquerade as unverified community artwork");
    auto& edited=officialRestored.onlineGames.at(saved.currentGameId);edited.isOfficial=true;edited.artworkMode=L"crop";
    edited.cropX=.25;edited.cropY=.75;edited.artworkZoom=1.5;edited.finalImageKey=std::wstring(64,L'a');edited.sourceImageKey=std::wstring(64,L'b');
    Check(officialRestored.Save(path.string()),"persist artwork recipe and durable file identities");
    Config editRestored;Check(editRestored.Load(path.string()),"restore artwork recipe without network");
    const auto& recipe=editRestored.onlineGames.at(saved.currentGameId);
    Check(recipe.artworkMode==L"crop"&&recipe.cropX==.25&&recipe.cropY==.75&&recipe.artworkZoom==1.5&&recipe.finalImageKey==edited.finalImageKey&&recipe.sourceImageKey==edited.sourceImageKey,
          "per-game recipe and both durable images survive config roundtrip");
    edited.finalImageKey=L"../outside";Check(!officialRestored.Save(path.string()),"reject caller-supplied filesystem paths");
    restored.onlineGames[saved.currentGameId].coverUrl = L"https://evil.test/cover.png";
    Check(!restored.Save(path.string()), "reject untrusted persisted art");
    std::error_code error; std::filesystem::remove(path, error);

    std::atomic<bool> started = false;
    GameService service(std::make_unique<SlowProvider>(started));
    Request old; old.operation = Operation::Search; old.request = 1; old.query = L"slow"; old.locale = L"zh-TW";
    service.Submit(old);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!started && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Check(started, "provider started off the caller thread");
    Request fresh = old; fresh.request = 2; fresh.query = L"new";
    const auto before = std::chrono::steady_clock::now(); service.Submit(fresh);
    Check(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100), "new request never joins the old request");
    bool received = false, stale = false;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!received && std::chrono::steady_clock::now() < deadline) {
        for (const auto& result : service.TakeResults()) if (result.operation == Operation::Search) {
            stale |= result.request == 1;
            received |= result.request == 2 && result.reply.games.size() == 1 && result.reply.games[0].id == 20;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(received && !stale, "stale provider results cannot overwrite the new query");
    // Seed already decoded native images so the real worker batch can be tested
    // without making requests to Nintendo or SteamGridDB.
    auto gallery=std::make_unique<GalleryProvider>();gallery->covers={communitySquare,officialPortrait};
    GameService galleryService(std::move(gallery));
    auto cached=std::make_shared<Image>();cached->width=cached->height=1;
    cached->png={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
        0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,165,246,69,64,0,0,0,0,73,69,78,68,174,66,96,130};
    galleryService.CacheDiscordImage(cached,communitySquare.url);galleryService.CacheDiscordImage(cached,officialPortrait.url);
    std::atomic<bool> labelsStarted=false;
    GameService libraryFirst(std::make_unique<SlowProvider>(labelsStarted));
    libraryFirst.CacheDiscordImage(cached,communitySquare.url);
    Request libraryBatch;libraryBatch.operation=Operation::LibraryImages;libraryBatch.locale=L"zh-TW";libraryBatch.accountOriginal=true;
    SavedGame first;first.key="xbox-1";first.name=L"slow";first.provider=L"xbox-account";first.coverUrl=communitySquare.url;
    auto second=first;second.key="xbox-2";second.name=L"Second";second.displayNames[L"zh-TW"]=L"第二款";
    libraryBatch.library={first,second};libraryFirst.Submit(libraryBatch);
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!labelsStarted&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    unsigned readyBeforeLabels=0;for(const auto& result:libraryFirst.TakeResults())if(result.operation==Operation::LibraryImages&&!result.imageUri.empty())++readyBeforeLabels;
    Check(labelsStarted&&readyBeforeLabels==2,"all account library images are published before a slow name lookup");libraryFirst.Cancel();
    Request batch;batch.operation=Operation::Covers;batch.gameId=5303117;batch.request=91;batch.locale=L"zh-TW";batch.shape=L"all";
    SavedGame visible;visible.key="xbox-1";visible.name=L"Visible library image";visible.coverUrl=communitySquare.url;visible.displayNames[L"zh-TW"]=visible.name;batch.library={visible};
    galleryService.Submit(batch);
    bool complete=false,header=false,libraryReady=false;unsigned imageCount=0;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!complete&&std::chrono::steady_clock::now()<deadline){
        for(const auto& result:galleryService.TakeResults()){
            if(result.operation==Operation::LibraryImages&&result.key==visible.key){Check(!result.imageUri.empty(),"cached library art remains immediately available");libraryReady=true;}
            if(result.operation==Operation::Covers){Check(libraryReady,"visible library images precede the gallery catalogue and image batch");header=true;}
            if(result.operation==Operation::Image){Check(!complete,"all native image results precede completion");++imageCount;}
            if(result.operation==Operation::CoverImagesComplete){
                complete=true;
                Check(header&&imageCount==2&&result.request==91&&result.reply.covers.size()==2&&
                    PreferredActivityArtwork(result.reply.covers,result.locale)->id==officialPortrait.id&&result.reply.covers[0].shape==L"square",
                    "real native worker exposes the complete loaded candidate set only after every image");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(complete,"cover batch always publishes a terminal readiness notification");
    galleryService.Cancel();Check(galleryService.TakeResults().empty(),"cancellation removes stale batch completion");
    batch.request=92;batch.searchRequest=93;Game pendingThumbnail;pendingThumbnail.id=200;pendingThumbnail.name=L"Pending thumbnail";batch.searchGames={pendingThumbnail};
    galleryService.Submit(batch);bool resumedThumbnail=false;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!resumedThumbnail&&std::chrono::steady_clock::now()<deadline){
        for(const auto& result:galleryService.TakeResults())if(result.operation==Operation::Image&&!result.coverId&&result.request==93&&result.gameId==200)resumedThumbnail=true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(resumedThumbnail,"cover requests resume pending search thumbnails using the original search request ID");
    {
        auto provider=std::make_unique<OfficialThumbnailProvider>();auto* observed=provider.get();
        provider->game={700001u,0,L"Official square fixture",L"官方正方形測試",L"fixture"};
        provider->game.artworkSources={taiwanLandscape};
        GameService thumbnails(std::move(provider));
        auto accountSquare=taiwanLandscape;accountSquare.provider=L"nintendo-account";accountSquare.shape.clear();
        accountSquare.url=accountSquare.thumb=L"https://atum-img-lp1.cdn.nintendo.net/i/c/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_512";
        thumbnails.CacheDiscordImage(cached,accountSquare.url);
        Request search;search.operation=Operation::Search;search.request=94;search.query=L"Official square fixture";search.locale=L"zh-TW";search.platform=L"switch";
        SavedGame account;account.key="nintendo-BEE-0400000000000001";account.name=L"Official square fixture";account.provider=L"nintendo-account";account.coverUrl=accountSquare.url;account.displayNames[L"zh-TW"]=L"官方正方形測試";
        search.library={account};search.libraryArtworks[account.key]={accountSquare};thumbnails.Submit(search);
        bool ready=false;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!ready&&std::chrono::steady_clock::now()<deadline){
            for(const auto& result:thumbnails.TakeResults())if(result.operation==Operation::Image&&result.gameId==700001u)ready=result.imageStatus==L"ok"&&result.shape==L"square"&&!result.reply.covers.empty()&&result.reply.covers.front().provider==L"nintendo-account";
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(ready&&observed->communityRequests==0,"native search selects a decoded official account square ahead of a landscape and avoids community requests");
    }
    auto officialProvider=CreateOfficialArtworkProvider();
    for(const auto gameId:{5454475u,5296432u}){
        const auto officialGame=officialProvider->Lookup(gameId,L"zh-TW");
        Check(officialGame.has_value()&&!officialGame->artworkSources.empty(),"existing PlayStation artwork is registered");
        if(!officialGame||officialGame->artworkSources.empty())continue;
        auto provider=std::make_unique<OfficialThumbnailProvider>();provider->game=*officialGame;
        auto* observed=provider.get();GameService thumbnails(std::move(provider));
        thumbnails.CacheDiscordImage(cached,officialGame->artworkSources.front().url);
        Request search;search.operation=Operation::Search;search.request=92;search.query=L"official thumbnail fixture";
        search.locale=L"zh-TW";search.platform=L"playstation";thumbnails.Submit(search);
        bool thumbnailReady=false;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!thumbnailReady&&std::chrono::steady_clock::now()<deadline){
            for(const auto& result:thumbnails.TakeResults())if(result.operation==Operation::Image&&result.gameId==gameId){
                thumbnailReady=result.imageStatus==L"ok"&&!result.imageUri.empty()&&result.shape==L"square"&&
                    !result.reply.covers.empty()&&result.reply.covers.front().provider==L"playstation-tw";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(thumbnailReady&&observed->communityRequests==0,
              "native search uses a registered PlayStation square without falling through to community artwork");
    }
    return failures ? 1 : 0;
}
