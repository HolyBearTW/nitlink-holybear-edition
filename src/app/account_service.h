#pragma once
#include "xbox_auth_callback.h"
#include "nintendo_auth_callback.h"
#include "psn_auth_browser.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace NitLink::Accounts {
enum class Provider { Nintendo, PlayStation, Xbox };
enum class Operation { BeginLink, FinishLink, Sync, Disconnect,
    CancelXboxLogin, CompleteXboxLogin, XboxLoginFailed,
    BeginManualLink, CancelNativeLogin, CompleteNintendoLogin, CompletePsnLogin, NativeLoginFailed };
// NitLink's public Microsoft application Client ID. Maintainer setup: docs/XBOX-AUTH.md.
inline constexpr wchar_t XboxClientId[] = L"6b9b7a75-e9fc-4fe5-813e-a4d8ad925815";
inline constexpr std::array<std::wstring_view,6> SupportedLocales={L"zh-TW",L"zh-CN",L"en-US",L"de-DE",L"ja-JP",L"ko-KR"};
inline std::optional<Provider> ParseProvider(std::wstring_view value) {
    if(value==L"nintendo")return Provider::Nintendo;if(value==L"playstation")return Provider::PlayStation;if(value==L"xbox")return Provider::Xbox;return std::nullopt;
}
inline std::wstring_view ProviderName(Provider provider) {
    constexpr std::array<std::wstring_view,3> names={L"nintendo",L"playstation",L"xbox"};return names[static_cast<size_t>(provider)];
}

struct Artwork { std::wstring url, kind, shape, language; };
struct ArtworkImage { std::wstring image, shape, status; };
struct Game {
    std::string key;
    std::wstring titleId, accountId, name, platform, imageUrl, firstPlayed, lastPlayed, nameLocale;
    std::optional<uint64_t> totalMinutes;
    Provider provider = Provider::Nintendo;
    std::vector<Artwork> artworks;
    std::map<std::wstring,std::wstring> displayNames;
};
struct Request {
    Operation operation;
    Provider provider;
    std::wstring value;
    std::wstring locale=L"en-US";
    uint64_t attempt=0; // Internal callback correlation; never accepted from a WebView message.
};

// One worker owns credentials, token refresh and platform requests. UI snapshots
// contain public account/game data only. No HTTP requests run on the UI thread.
class AccountService {
public:
    AccountService();
    ~AccountService();
    void Submit(Request request);
    bool TakeChanged();
    std::vector<Game> Games() const;
    std::optional<Game> FindGame(std::string_view key) const;
    std::wstring StateJson(const std::map<std::string, std::wstring>& images,const std::map<std::wstring,ArtworkImage>& artworkImages,const std::map<std::string,std::map<std::wstring,std::wstring>>& names) const;
private:
    struct State {
        std::wstring status=L"disconnected", error, accountId, nickname;
        uint64_t syncedAt=0;
        uint64_t linkRevision=0; // Successful sign-ins in this process; never persisted with credentials.
        std::vector<Game> games;
    };
    struct Credential {
        std::wstring token, verifier, state, redirect;
    };
    void Run(std::stop_token stop);
    void Execute(const Request& request);
    void Sync(Provider provider, std::wstring accessToken = {},std::wstring locale=L"en-US");
    void Save();
    void Load();
    std::filesystem::path m_path;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Request> m_requests;
    std::array<State,3> m_states;
    std::array<Credential,3> m_credentials;
    std::atomic<bool> m_changed{true};
    NintendoAuthCallback m_nintendoCallback;
    PsnAuthBrowser m_psnBrowser;
    std::array<uint64_t,2> m_nativeAttempts{};
    XboxAuthCallback m_xboxCallback;
    uint64_t m_xboxAttempt=0;
    std::jthread m_worker;
};
} // namespace NitLink::Accounts
