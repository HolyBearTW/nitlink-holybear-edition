#include "account_service.h"
#include "game_identity.h"

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cwctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <utility>

namespace NitLink::Accounts {
using namespace winrt::Windows::Data::Json;
namespace {
constexpr wchar_t NintendoClient[] = L"5c38e31cd085304b";
constexpr wchar_t PsnBasic[] = L"Authorization: Basic MDk1MTUxNTktNzIzNy00MzcwLTliNDAtMzgwNmU2N2MwODkxOnVjUGprYTV0bnRCMktxc1A=\r\n";
struct Failure { std::wstring key; };
struct InternetCloser { void operator()(void* handle) const { if(handle) WinHttpCloseHandle(handle); } };
using Internet = std::unique_ptr<void,InternetCloser>;
struct HttpReply { DWORD status=0; std::wstring body, location; };
size_t Index(Provider provider) { return static_cast<size_t>(provider); }
uint64_t Now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::wstring Str(const JsonObject& object, std::wstring_view key) { return std::wstring(object.GetNamedString(winrt::hstring(key),L"")); }
JsonArray Array(const JsonObject& object, std::wstring_view key) { return object.GetNamedArray(winrt::hstring(key),JsonArray{}); }
void Put(JsonObject& object,std::wstring_view key,std::wstring_view value) { object.SetNamedValue(winrt::hstring(key),JsonValue::CreateStringValue(winrt::hstring(value))); }
void Put(JsonObject& object,std::wstring_view key,uint64_t value) { object.SetNamedValue(winrt::hstring(key),JsonValue::CreateNumberValue(static_cast<double>(value))); }
std::wstring Encode(std::wstring_view value) {
    const auto bytes=winrt::to_string(winrt::hstring(value)); std::wstring out;
    constexpr wchar_t hex[]=L"0123456789ABCDEF";
    for(unsigned char c:bytes) {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=c;
        else {out+=L'%';out+=hex[c>>4];out+=hex[c&15];}
    }
    return out;
}
std::wstring Form(std::initializer_list<std::pair<std::wstring_view,std::wstring_view>> values) {
    std::wstring out;
    for(const auto& [key,value]:values){if(!out.empty())out+=L'&';out+=Encode(key)+L"="+Encode(value);}
    return out;
}
std::wstring Random() {
    std::array<unsigned char,32> bytes{};
    if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw Failure{L"auth"};
    std::wstring out;constexpr wchar_t hex[]=L"0123456789abcdef";
    for(auto c:bytes){out+=hex[c>>4];out+=hex[c&15];}return out;
}
std::wstring Challenge(std::wstring_view verifier) {
    const auto input=winrt::to_string(winrt::hstring(verifier));std::array<unsigned char,32> digest{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),static_cast<ULONG>(input.size()),digest.data(),static_cast<ULONG>(digest.size()))<0)throw Failure{L"auth"};
    DWORD size=0;CryptBinaryToStringW(digest.data(),static_cast<DWORD>(digest.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&size);
    std::wstring result(size,L'\0');
    if(!CryptBinaryToStringW(digest.data(),static_cast<DWORD>(digest.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,result.data(),&size))throw Failure{L"auth"};
    result.resize(size);while(!result.empty()&&(result.back()==L'\0'||result.back()==L'='))result.pop_back();
    std::replace(result.begin(),result.end(),L'+',L'-');std::replace(result.begin(),result.end(),L'/',L'_');return result;
}
HttpReply Http(const wchar_t* method,const wchar_t* host,const std::wstring& path,std::wstring_view body={},std::wstring headers={},const wchar_t* agent=L"NitLink-HolyBear-Accounts/1") {
    Internet session(WinHttpOpen(agent,WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session)throw Failure{L"network"};
    WinHttpSetTimeouts(session.get(),8000,8000,15000,15000);
    Internet connection(WinHttpConnect(session.get(),host,INTERNET_DEFAULT_HTTPS_PORT,0));
    Internet request(connection?WinHttpOpenRequest(connection.get(),method,path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr);
    if(!request)throw Failure{L"network"};
    // OAuth redirects are read as data; credentials never follow redirects.
    DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(request.get(),WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled));
    headers+=L"Accept: application/json\r\n";
    const auto bytes=winrt::to_string(winrt::hstring(body));
    if(!WinHttpSendRequest(request.get(),headers.c_str(),static_cast<DWORD>(headers.size()),bytes.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(bytes.data()),static_cast<DWORD>(bytes.size()),static_cast<DWORD>(bytes.size()),0)||!WinHttpReceiveResponse(request.get(),nullptr))throw Failure{L"network"};
    HttpReply result;DWORD size=sizeof(result.status);
    if(!WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&result.status,&size,WINHTTP_NO_HEADER_INDEX))throw Failure{L"network"};
    size=0;WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&size,WINHTTP_NO_HEADER_INDEX);
    if(size){std::wstring location(size/sizeof(wchar_t),L'\0');if(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,location.data(),&size,WINHTTP_NO_HEADER_INDEX)){location.resize(size/sizeof(wchar_t));while(!location.empty()&&location.back()==L'\0')location.pop_back();result.location=std::move(location);}}
    std::string response;std::array<char,16384> buffer{};DWORD read=0;
    do {if(!WinHttpReadData(request.get(),buffer.data(),static_cast<DWORD>(buffer.size()),&read))throw Failure{L"network"};response.append(buffer.data(),read);if(response.size()>8*1024*1024)throw Failure{L"invalidData"};}while(read);
    result.body=winrt::to_hstring(response);return result;
}
JsonObject Json(const HttpReply& reply) {
    if(reply.status==401)throw Failure{L"expired"};
    if(reply.status==403)throw Failure{L"permission"};
    if(reply.status==429)throw Failure{L"rateLimited"};
    if(reply.status<200||reply.status>=300) {
        JsonObject error;
        if(JsonObject::TryParse(reply.body,error)){
            const auto reason=error.TryLookup(L"error");
            if(reason&&reason.ValueType()==JsonValueType::String&&reason.GetString()==L"invalid_grant")throw Failure{L"expired"};
        }
        throw Failure{reply.status==400?L"auth":L"network"};
    }
    return JsonObject::Parse(reply.body);
}
std::wstring Parameter(std::wstring_view uri,std::wstring_view name) {
    const auto split=uri.find_first_of(L"?#");if(split==uri.npos)return {};
    auto query=std::wstring(uri.substr(split+1));std::replace(query.begin(),query.end(),L'#',L'&');
    winrt::Windows::Foundation::WwwFormUrlDecoder decoder(query);
    for(const auto& pair:decoder)if(pair.Name()==name)return std::wstring(pair.Value());
    return {};
}
std::optional<uint64_t> Minutes(const JsonObject& object,const wchar_t* name) {
    const auto value=object.TryLookup(name);if(!value||value.ValueType()==JsonValueType::Null)return std::nullopt;
    double number=0;
    if(value.ValueType()==JsonValueType::Number)number=value.GetNumber();
    else if(value.ValueType()==JsonValueType::String){const auto bytes=winrt::to_string(value.GetString());const auto parsed=std::from_chars(bytes.data(),bytes.data()+bytes.size(),number);if(parsed.ec!=std::errc{}||parsed.ptr!=bytes.data()+bytes.size())throw Failure{L"invalidData"};}
    else throw Failure{L"invalidData"};
    if(!std::isfinite(number)||number<0||number>9007199254740991.0)throw Failure{L"invalidData"};
    return static_cast<uint64_t>(number);
}
void ValidateGame(Game& game) {
    // External platform data enters the typed model here, once.
    if(game.titleId.empty()||game.titleId.size()>64||!NitLink::Games::ValidText(game.name)||game.name.empty()||game.accountId.empty())throw Failure{L"invalidData"};
    if(game.provider==Provider::Nintendo)std::transform(game.titleId.begin(),game.titleId.end(),game.titleId.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towupper(c));});
    game.key=game.provider==Provider::Nintendo?(game.platform==L"Switch 2"?"nintendo-BEE-":"nintendo-HAC-")+winrt::to_string(game.titleId):
        winrt::to_string(winrt::hstring(ProviderName(game.provider)))+"-"+winrt::to_string(game.titleId);
    if(!NitLink::Games::IsAccountGameKey(game.key))throw Failure{L"invalidData"};
    const auto artworkLocale=game.nameLocale;
    // A requested language is not proof that the platform translated its title.
    if(NitLink::Games::NeedsLocalizedTitle(game.name,game.nameLocale))game.nameLocale=L"en-US";
    if(NitLink::Games::IsLocale(game.nameLocale))game.displayNames[game.nameLocale]=game.name;
    if(game.displayNames.size()>SupportedLocales.size()||std::any_of(game.displayNames.begin(),game.displayNames.end(),[](const auto& label){return !NitLink::Games::IsLocale(label.first)||!NitLink::Games::ValidText(label.second);}))throw Failure{L"invalidData"};
    std::erase_if(game.displayNames,[](const auto& label){return NitLink::Games::NeedsLocalizedTitle(label.second,label.first);});
    if(!game.imageUrl.empty()&&std::none_of(game.artworks.begin(),game.artworks.end(),[&](const Artwork& artwork){return artwork.url==game.imageUrl;}))game.artworks.insert(game.artworks.begin(),{game.imageUrl,L"gameImage",L"",artworkLocale});
    std::vector<Artwork> usable;
    for(auto& artwork:game.artworks){if(artwork.url.starts_with(L"http://")){auto secure=L"https://"+artwork.url.substr(7);if(NitLink::Games::IsAccountArtworkUrl(secure))artwork.url=std::move(secure);}}
    for(auto& artwork:game.artworks)if(NitLink::Games::IsAccountArtworkUrl(artwork.url)&&std::none_of(usable.begin(),usable.end(),[&](const auto& item){return item.url==artwork.url;}))usable.push_back(std::move(artwork));
    game.artworks=std::move(usable);game.imageUrl=game.artworks.empty()?L"":game.artworks.front().url;
}
JsonObject GameJson(const Game& game) {
    JsonObject out;Put(out,L"key",winrt::to_hstring(game.key));Put(out,L"provider",ProviderName(game.provider));Put(out,L"titleId",game.titleId);Put(out,L"accountId",game.accountId);
    Put(out,L"name",game.name);Put(out,L"nameLocale",game.nameLocale);Put(out,L"platform",game.platform);Put(out,L"imageUrl",game.imageUrl);Put(out,L"firstPlayed",game.firstPlayed);Put(out,L"lastPlayed",game.lastPlayed);
    if(game.totalMinutes)Put(out,L"totalMinutes",*game.totalMinutes);else out.SetNamedValue(L"totalMinutes",JsonValue::Parse(L"null"));
    JsonObject labels;for(const auto& [locale,name]:game.displayNames)Put(labels,locale,name);out.SetNamedValue(L"displayNames",labels);
    JsonArray artworks;for(const auto& artwork:game.artworks){JsonObject item;Put(item,L"url",artwork.url);Put(item,L"kind",artwork.kind);Put(item,L"shape",artwork.shape);Put(item,L"language",artwork.language);artworks.Append(item);}out.SetNamedValue(L"artworks",artworks);return out;
}
}

AccountService::AccountService() {
    PWSTR directory=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&directory))){m_path=std::filesystem::path(directory)/L"NitLink"/L"linked-accounts.dat";CoTaskMemFree(directory);}
    try {Load();}catch(...){for(auto& state:m_states){state.error=L"storage";state.status=L"error";}}
    m_worker=std::jthread([this](std::stop_token stop){Run(stop);});
}
AccountService::~AccountService() {
    m_worker.request_stop();m_wake.notify_all();
    if(m_worker.joinable())m_worker.join(); // The worker alone starts/stops login attempts.
    m_xboxCallback.Stop(); // Join callbacks before the queue and its mutex are destroyed.
    m_nintendoCallback.Stop();m_psnBrowser.Stop();
}
void AccountService::Submit(Request request) {std::lock_guard lock(m_mutex);m_requests.push_back(std::move(request));m_wake.notify_one();}
bool AccountService::TakeChanged() {return m_changed.exchange(false);}
std::vector<Game> AccountService::Games() const {
    std::lock_guard lock(m_mutex);std::vector<Game> games;for(const auto& state:m_states)games.insert(games.end(),state.games.begin(),state.games.end());return games;
}
std::optional<Game> AccountService::FindGame(std::string_view key) const {
    std::lock_guard lock(m_mutex);for(const auto& state:m_states)for(const auto& game:state.games)if(game.key==key)return game;return std::nullopt;
}
std::wstring AccountService::StateJson(const std::map<std::string,std::wstring>& images,const std::map<std::wstring,ArtworkImage>& artworkImages,const std::map<std::string,std::map<std::wstring,std::wstring>>& names) const {
    std::lock_guard lock(m_mutex);JsonObject out;JsonArray accounts,games;
    for(size_t i=0;i<m_states.size();++i){const auto& state=m_states[i];JsonObject item;
        Put(item,L"provider",ProviderName(static_cast<Provider>(i)));Put(item,L"status",state.status);Put(item,L"error",state.error);Put(item,L"accountId",state.accountId);Put(item,L"nickname",state.nickname);Put(item,L"syncedAt",state.syncedAt);
        Put(item,L"linkRevision",state.linkRevision);accounts.Append(item);
        for(const auto& game:state.games){auto record=GameJson(game);auto labels=record.GetNamedObject(L"displayNames");if(const auto found=names.find(game.key);found!=names.end())for(const auto& [locale,name]:found->second)if(!game.displayNames.contains(locale))Put(labels,locale,name);record.SetNamedValue(L"displayNames",labels);const auto found=images.find(game.key);Put(record,L"image",found==images.end()?L"":std::wstring_view(found->second));JsonArray artworks;
            for(size_t index=0;index<game.artworks.size();++index){const auto& source=game.artworks[index];JsonObject item;Put(item,L"id",index+1);Put(item,L"url",source.url);Put(item,L"kind",source.kind);Put(item,L"language",source.language);const auto image=artworkImages.find(source.url);Put(item,L"image",image==artworkImages.end()?L"":std::wstring_view(image->second.image));Put(item,L"shape",image==artworkImages.end()?source.shape:std::wstring_view(image->second.shape));Put(item,L"status",image==artworkImages.end()?L"loading":std::wstring_view(image->second.status));artworks.Append(item);}
            record.SetNamedValue(L"artworks",artworks);games.Append(record);}
    }
    out.SetNamedValue(L"accounts",accounts);out.SetNamedValue(L"games",games);out.SetNamedValue(L"preview",JsonValue::CreateBooleanValue(false));
    JsonObject root;root.SetNamedValue(L"accountLibrary",out);return std::wstring(root.Stringify());
}
void AccountService::Load() {
    if(m_path.empty()||!std::filesystem::exists(m_path))return;
    std::ifstream file(m_path,std::ios::binary);std::vector<unsigned char> encrypted((std::istreambuf_iterator<char>(file)),{});
    if(encrypted.empty()||encrypted.size()>32*1024*1024)throw Failure{L"storage"};
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),encrypted.data()},output{};
    if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))throw Failure{L"storage"};
    std::string plain(reinterpret_cast<char*>(output.pbData),output.cbData);LocalFree(output.pbData);
    const auto root=JsonObject::Parse(winrt::to_hstring(plain));
    const auto accounts=Array(root,L"accounts");
    for(const auto& entry:accounts){const auto item=entry.GetObject();const auto provider=ParseProvider(Str(item,L"provider"));if(!provider)throw Failure{L"invalidData"};
        auto& credential=m_credentials[Index(*provider)];auto& state=m_states[Index(*provider)];credential.token=Str(item,L"token");state.accountId=Str(item,L"accountId");state.nickname=Str(item,L"nickname");state.syncedAt=Minutes(item,L"syncedAt").value_or(0);state.status=credential.token.empty()?L"disconnected":L"connected";
        for(const auto& record:Array(item,L"games")){const auto value=record.GetObject();Game game;game.provider=*provider;game.titleId=Str(value,L"titleId");game.accountId=state.accountId;game.name=Str(value,L"name");game.nameLocale=Str(value,L"nameLocale");for(const auto& label:value.GetNamedObject(L"displayNames",JsonObject{}))game.displayNames[std::wstring(label.Key())]=std::wstring(label.Value().GetString());game.platform=Str(value,L"platform");game.imageUrl=Str(value,L"imageUrl");game.firstPlayed=Str(value,L"firstPlayed");game.lastPlayed=Str(value,L"lastPlayed");game.totalMinutes=Minutes(value,L"totalMinutes");for(const auto& image:Array(value,L"artworks")){const auto artwork=image.GetObject();game.artworks.push_back({Str(artwork,L"url"),Str(artwork,L"kind"),Str(artwork,L"shape"),Str(artwork,L"language")});}ValidateGame(game);state.games.push_back(std::move(game));}
    }
}
void AccountService::Save() {
    JsonObject root;JsonArray accounts;
    {std::lock_guard lock(m_mutex);
        for(size_t i=0;i<m_states.size();++i){const auto& state=m_states[i];JsonObject item;Put(item,L"provider",ProviderName(static_cast<Provider>(i)));Put(item,L"token",m_credentials[i].token);Put(item,L"accountId",state.accountId);Put(item,L"nickname",state.nickname);Put(item,L"syncedAt",state.syncedAt);JsonArray games;for(const auto& game:state.games)games.Append(GameJson(game));item.SetNamedValue(L"games",games);accounts.Append(item);}
    }
    root.SetNamedValue(L"accounts",accounts);const auto plain=winrt::to_string(root.Stringify());
    DATA_BLOB input{static_cast<DWORD>(plain.size()),reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))},output{};
    if(m_path.empty()||!CryptProtectData(&input,L"NitLink linked accounts",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))throw Failure{L"storage"};
    std::vector<unsigned char> encrypted(output.pbData,output.pbData+output.cbData);LocalFree(output.pbData);
    std::filesystem::create_directories(m_path.parent_path());std::ofstream file(m_path,std::ios::binary|std::ios::trunc);
    if(!file.write(reinterpret_cast<const char*>(encrypted.data()),static_cast<std::streamsize>(encrypted.size())))throw Failure{L"storage"};
}
void AccountService::Run(std::stop_token stop) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    while(!stop.stop_requested()) {
        Request request;
        {std::unique_lock lock(m_mutex);m_wake.wait(lock,[&]{return stop.stop_requested()||!m_requests.empty();});if(stop.stop_requested())break;request=std::move(m_requests.front());m_requests.pop_front();}
        try {Execute(request);}
        catch(const Failure& failure){std::lock_guard lock(m_mutex);auto& state=m_states[Index(request.provider)];state.error=failure.key;state.status=failure.key==L"expired"?L"expired":L"error";m_changed=true;}
        catch(const winrt::hresult_error&){std::lock_guard lock(m_mutex);auto& state=m_states[Index(request.provider)];state.error=L"invalidData";state.status=L"error";m_changed=true;}
        catch(const std::exception&){std::lock_guard lock(m_mutex);auto& state=m_states[Index(request.provider)];state.error=L"storage";state.status=L"error";m_changed=true;}
    }
}
void AccountService::Execute(const Request& request) {
    const auto provider=request.provider;const auto i=Index(provider);auto& credential=m_credentials[i];
    if(request.operation==Operation::CompleteNintendoLogin||request.operation==Operation::CompletePsnLogin||request.operation==Operation::NativeLoginFailed){
        if(request.attempt!=m_nativeAttempts[i]||credential.state.empty())return;
        if(provider==Provider::Nintendo)m_nintendoCallback.Stop();else m_psnBrowser.Stop();
        if(request.operation==Operation::NativeLoginFailed){
            ++m_nativeAttempts[i];credential.state.clear();credential.verifier.clear();
            std::lock_guard lock(m_mutex);auto& state=m_states[i];state.status=state.accountId.empty()?L"disconnected":L"connected";state.error=request.value;m_changed=true;return;
        }
    }
    if(request.operation==Operation::CancelNativeLogin){
        if(credential.state.empty())return;
        if(provider==Provider::Nintendo)m_nintendoCallback.Stop();else m_psnBrowser.Stop();
        ++m_nativeAttempts[i];credential.state.clear();credential.verifier.clear();
        std::lock_guard lock(m_mutex);auto& state=m_states[i];state.status=state.accountId.empty()?L"disconnected":L"connected";state.error=L"cancelled";m_changed=true;return;
    }
    if(provider!=Provider::Xbox&&(request.operation==Operation::BeginLink||request.operation==Operation::BeginManualLink||request.operation==Operation::Disconnect||request.operation==Operation::FinishLink)){
        if(provider==Provider::Nintendo)m_nintendoCallback.Stop();else m_psnBrowser.Stop();
        ++m_nativeAttempts[i];
    }
    if(request.operation==Operation::CompleteXboxLogin||request.operation==Operation::XboxLoginFailed){
        if(request.attempt!=m_xboxAttempt||credential.state.empty())return; // Ignore a cancelled or already consumed attempt.
        m_xboxCallback.Stop();
        if(request.operation==Operation::XboxLoginFailed){
            credential.state.clear();credential.verifier.clear();credential.redirect.clear();++m_xboxAttempt;
            std::lock_guard lock(m_mutex);auto& state=m_states[i];
            state.status=state.accountId.empty()?L"disconnected":L"connected";state.error=request.value;m_changed=true;return;
        }
    }
    if(request.operation==Operation::CancelXboxLogin){
        if(credential.state.empty())return;
        m_xboxCallback.Stop();++m_xboxAttempt;
        credential.state.clear();credential.verifier.clear();credential.redirect.clear();
        std::lock_guard lock(m_mutex);auto& state=m_states[i];
        state.status=state.accountId.empty()?L"disconnected":L"connected";state.error=L"cancelled";m_changed=true;return;
    }
    if(provider==Provider::Xbox&&(request.operation==Operation::BeginLink||request.operation==Operation::Disconnect)){
        m_xboxCallback.Stop();++m_xboxAttempt;
        credential.state.clear();credential.verifier.clear();credential.redirect.clear();
    }
    if(request.operation==Operation::Disconnect){{std::lock_guard lock(m_mutex);m_states[i]={};credential={};}Save();m_changed=true;return;}
    if(request.operation==Operation::BeginLink||request.operation==Operation::BeginManualLink){
        credential.state=Random();credential.verifier=Random();std::wstring url;
        if(provider==Provider::Nintendo)url=L"https://accounts.nintendo.com/connect/1.0.0/authorize?"+Form({{L"client_id",NintendoClient},{L"redirect_uri",L"npf5c38e31cd085304b://auth"},{L"scope",L"openid user user.mii user.email user.links[].id"},{L"response_type",L"session_token_code"},{L"session_token_code_challenge_method",L"S256"},{L"session_token_code_challenge",Challenge(credential.verifier)},{L"state",credential.state},{L"theme",L"login_form"}});
        else if(provider==Provider::PlayStation)url=L"https://www.playstation.com/acct/management/";
        else {
            const auto attempt=m_xboxAttempt;
            const auto redirect=m_xboxCallback.Start(credential.state,[this,attempt,locale=request.locale](XboxAuthCallback::Result result){
                if(result.outcome==XboxAuthCallback::Outcome::Code)Submit({Operation::CompleteXboxLogin,Provider::Xbox,std::move(result.code),locale,attempt});
                else Submit({Operation::XboxLoginFailed,Provider::Xbox,
                    result.outcome==XboxAuthCallback::Outcome::Timeout?L"loginTimeout":result.outcome==XboxAuthCallback::Outcome::Denied?L"cancelled":result.outcome==XboxAuthCallback::Outcome::Rejected?L"auth":L"callbackListen",locale,attempt});
            });
            if(!redirect){credential.state.clear();credential.verifier.clear();credential.redirect.clear();throw Failure{L"callbackListen"};}
            credential.redirect=*redirect;
            url=L"https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize?"+Form({{L"client_id",XboxClientId},{L"response_type",L"code"},{L"response_mode",L"query"},{L"redirect_uri",credential.redirect},{L"scope",L"XboxLive.signin offline_access"},{L"code_challenge_method",L"S256"},{L"code_challenge",Challenge(credential.verifier)},{L"state",credential.state}});
        }
        bool openBrowser=true;
        if(provider!=Provider::Xbox&&request.operation==Operation::BeginLink){
            const auto attempt=m_nativeAttempts[i];
            if(provider==Provider::Nintendo){
                const auto registered=m_nintendoCallback.Start(credential.state,[this,attempt,locale=request.locale](NintendoAuthCallback::Result result){
                    if(result.outcome==NintendoAuthCallback::Outcome::Code)Submit({Operation::CompleteNintendoLogin,Provider::Nintendo,std::move(result.code),locale,attempt});
                    else Submit({Operation::NativeLoginFailed,Provider::Nintendo,result.outcome==NintendoAuthCallback::Outcome::Timeout?L"accountLoginTimeout":result.outcome==NintendoAuthCallback::Outcome::Denied?L"cancelled":L"auth",locale,attempt});
                });
                if(registered!=NintendoAuthCallback::StartResult::Ready){credential.state.clear();credential.verifier.clear();throw Failure{registered==NintendoAuthCallback::StartResult::InUse?L"protocolInUse":L"protocolRegistration"};}
            }else {
                openBrowser=false;
                m_psnBrowser.Start([this,attempt,locale=request.locale](PsnAuthBrowser::Result result){
                    if(result.outcome==PsnAuthBrowser::Outcome::Cookie)Submit({Operation::CompletePsnLogin,Provider::PlayStation,std::move(result.npsso),locale,attempt});
                    else Submit({Operation::NativeLoginFailed,Provider::PlayStation,result.outcome==PsnAuthBrowser::Outcome::Cancelled?L"cancelled":result.outcome==PsnAuthBrowser::Outcome::Timeout?L"accountLoginTimeout":L"psnBrowser",locale,attempt});
                });
            }
        }
        if(openBrowser&&reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32){
            if(provider==Provider::Xbox){m_xboxCallback.Stop();credential.state.clear();credential.verifier.clear();credential.redirect.clear();}
            else {if(provider==Provider::Nintendo)m_nintendoCallback.Stop();else m_psnBrowser.Stop();credential.state.clear();credential.verifier.clear();}
            throw Failure{L"auth"};
        }
        {std::lock_guard lock(m_mutex);m_states[i].status=L"awaiting";m_states[i].error.clear();}m_changed=true;return;
    }
    {std::lock_guard lock(m_mutex);m_states[i].status=L"loading";m_states[i].error.clear();}m_changed=true;
    if(request.operation==Operation::Sync){Sync(provider,{},request.locale);return;}
    std::wstring access;
    if(provider==Provider::PlayStation){
        ++m_nativeAttempts[i];credential.state.clear();credential.verifier.clear();
        std::wstring npsso=request.value;
        if(npsso.starts_with(L"{")){
            JsonObject response;
            if(!JsonObject::TryParse(npsso,response)||!response.HasKey(L"npsso")||response.GetNamedValue(L"npsso").ValueType()!=JsonValueType::String)throw Failure{L"psnInput"};
            npsso=Str(response,L"npsso");
        }else if(npsso.size()==66&&npsso.front()==L'"'&&npsso.back()==L'"')npsso=npsso.substr(1,64);
        if(npsso.size()!=64||!std::all_of(npsso.begin(),npsso.end(),[](wchar_t c){return c==0x21||(c>=0x23&&c<=0x2b)||(c>=0x2d&&c<=0x3a)||(c>=0x3c&&c<=0x5b)||(c>=0x5d&&c<=0x7e);}))throw Failure{L"psnInput"};
        const auto reply=Http(L"GET",L"ca.account.sony.com",L"/api/authz/v3/oauth/authorize?"+Form({{L"access_type",L"offline"},{L"client_id",L"09515159-7237-4370-9b40-3806e67c0891"},{L"redirect_uri",L"com.scee.psxandroid.scecompcall://redirect"},{L"response_type",L"code"},{L"scope",L"psn:mobile.v2.core psn:clientapp"}}),{},L"Cookie: npsso="+npsso+L"\r\n");
        if(reply.status==429)throw Failure{L"rateLimited"};
        if(reply.status>=500)throw Failure{L"network"};
        if(!reply.location.starts_with(L"com.scee.psxandroid.scecompcall://redirect"))throw Failure{L"psnCode"};const auto code=Parameter(reply.location,L"code");if(code.empty())throw Failure{L"psnCode"};
        const auto token=Json(Http(L"POST",L"ca.account.sony.com",L"/api/authz/v3/oauth/token",Form({{L"code",code},{L"redirect_uri",L"com.scee.psxandroid.scecompcall://redirect"},{L"grant_type",L"authorization_code"},{L"token_format",L"jwt"}}),std::wstring(PsnBasic)+L"Content-Type: application/x-www-form-urlencoded\r\n"));
        credential.token=Str(token,L"refresh_token");access=Str(token,L"access_token");
    }else if(provider==Provider::Nintendo){
        std::wstring code;
        if(request.operation==Operation::CompleteNintendoLogin)code=request.value; // Validated once by the protocol receiver.
        else {
            const auto target=std::wstring_view(request.value).substr(0,request.value.find(L'#'));
            if((target!=L"npf5c38e31cd085304b://auth"&&target!=L"npf5c38e31cd085304b://auth/")||credential.state.empty()||Parameter(request.value,L"state")!=credential.state)throw Failure{L"callback"};
            code=Parameter(request.value,L"session_token_code");if(code.empty())throw Failure{L"auth"};
        }
        ++m_nativeAttempts[i];credential.state.clear();const auto verifier=std::exchange(credential.verifier,{});
        const auto token=Json(Http(L"POST",L"accounts.nintendo.com",L"/connect/1.0.0/api/session_token",Form({{L"client_id",NintendoClient},{L"session_token_code",code},{L"session_token_code_verifier",verifier}}),L"Content-Type: application/x-www-form-urlencoded\r\nX-Platform: Android\r\nX-ProductVersion: 2.0.0\r\n",L"NASDKAPI; Android"));credential.token=Str(token,L"session_token");
    }else {
        // Xbox codes enter only through CompleteXboxLogin, validated at the loopback boundary.
        m_xboxCallback.Stop();++m_xboxAttempt;credential.state.clear(); // Consume once, including failed token exchanges.
        auto verifier=std::exchange(credential.verifier,{}),redirect=std::exchange(credential.redirect,{});
        const auto token=Json(Http(L"POST",L"login.microsoftonline.com",L"/consumers/oauth2/v2.0/token",Form({{L"client_id",XboxClientId},{L"code",request.value},{L"grant_type",L"authorization_code"},{L"redirect_uri",redirect},{L"code_verifier",verifier},{L"scope",L"XboxLive.signin offline_access"}}),L"Content-Type: application/x-www-form-urlencoded\r\n"));credential.token=Str(token,L"refresh_token");access=Str(token,L"access_token");
    }
    if(credential.token.empty()||(provider==Provider::PlayStation&&access.empty()))throw Failure{provider==Provider::PlayStation?L"psnToken":L"auth"};credential.state.clear();credential.verifier.clear();
    // A new identity must never inherit the previous account's cached history.
    {std::lock_guard lock(m_mutex);m_states[i].games.clear();m_states[i].accountId.clear();m_states[i].nickname.clear();m_states[i].syncedAt=0;}
    Save();Sync(provider,std::move(access),request.locale);
    {std::lock_guard lock(m_mutex);++m_states[i].linkRevision;}m_changed=true;
}
void AccountService::Sync(Provider provider,std::wstring access,std::wstring locale) {
    auto& credential=m_credentials[Index(provider)];if(credential.token.empty())throw Failure{L"expired"};
    if(access.empty()){
        JsonObject token;
        if(provider==Provider::Nintendo){JsonObject body;Put(body,L"client_id",NintendoClient);Put(body,L"session_token",credential.token);Put(body,L"grant_type",L"urn:ietf:params:oauth:grant-type:jwt-bearer-session-token");token=Json(Http(L"POST",L"accounts.nintendo.com",L"/connect/1.0.0/api/token",body.Stringify(),L"Content-Type: application/json\r\n",L"com.nintendo.znej/3.2.0 (iOS/26.0.1)"));}
        else if(provider==Provider::PlayStation)token=Json(Http(L"POST",L"ca.account.sony.com",L"/api/authz/v3/oauth/token",Form({{L"refresh_token",credential.token},{L"grant_type",L"refresh_token"},{L"token_format",L"jwt"},{L"scope",L"psn:mobile.v2.core psn:clientapp"}}),std::wstring(PsnBasic)+L"Content-Type: application/x-www-form-urlencoded\r\n"));
        else token=Json(Http(L"POST",L"login.microsoftonline.com",L"/consumers/oauth2/v2.0/token",Form({{L"client_id",XboxClientId},{L"refresh_token",credential.token},{L"grant_type",L"refresh_token"},{L"scope",L"XboxLive.signin offline_access"}}),L"Content-Type: application/x-www-form-urlencoded\r\n"));
        access=Str(token,L"access_token");if(access.empty())throw Failure{L"auth"};const auto refresh=Str(token,L"refresh_token");if(!refresh.empty()){credential.token=refresh;Save();}
    }
    std::vector<Game> games;std::wstring accountId,nickname,xboxAuthorization;
    if(provider==Provider::Nintendo){
        const auto& supported=locale;
        const auto headers=L"Authorization: Bearer "+access+L"\r\ngentry-locale: "+supported+L"\r\n";
        const auto user=Json(Http(L"GET",L"api.accounts.nintendo.com",L"/2.0.0/users/me",{},headers));accountId=Str(user,L"id");nickname=Str(user,L"nickname");
        const auto history=Json(Http(L"GET",L"app-api.znej.nintendo.com",L"/api/v2.0/users/me/play_histories",{},headers,L"com.nintendo.znej/3.2.0 (iOS/26.0.1)"));
        if(!history.HasKey(L"playHistories"))throw Failure{L"invalidData"};
        for(const auto& entry:Array(history,L"playHistories")){
            const auto item=entry.GetObject();auto device=Str(item,L"platform");if(device.empty())device=Str(item,L"deviceType");
            const bool switch2=device==L"BEE"||device==L"Nintendo Switch 2";
            if(!switch2&&device!=L"HAC"&&device!=L"Nintendo Switch")continue;
            Game game;game.provider=provider;game.accountId=accountId;game.titleId=Str(item,L"titleId");game.name=Str(item,L"titleName");game.nameLocale=supported;game.platform=switch2?L"Switch 2":L"Switch";
            game.imageUrl=Str(item,L"imageUrl");game.firstPlayed=Str(item,L"firstPlayedAt");game.lastPlayed=Str(item,L"lastPlayedAt");game.totalMinutes=Minutes(item,L"totalPlayedMinutes");ValidateGame(game);games.push_back(std::move(game));
        }
    }else if(provider==Provider::PlayStation){
        const auto headers=L"Authorization: Bearer "+access+L"\r\nAccept-Language: "+(locale==L"zh-TW"?std::wstring(L"zh-Hant-TW"):locale)+L"\r\n";
        // This endpoint requires a numeric account ID and does not accept "me".
        // Use account_id from Sony's HTTPS-issued token for routing; Sony validates the bearer token itself.
        const auto first=access.find(L'.'),last=access.find(L'.',first==access.npos?0:first+1);
        if(first==access.npos||last==access.npos)throw Failure{L"psnProfile"};
        auto payload=access.substr(first+1,last-first-1);std::replace(payload.begin(),payload.end(),L'-',L'+');std::replace(payload.begin(),payload.end(),L'_',L'/');
        while(payload.size()%4)payload+=L'=';
        DWORD size=0;
        if(!CryptStringToBinaryW(payload.c_str(),static_cast<DWORD>(payload.size()),CRYPT_STRING_BASE64,nullptr,&size,nullptr,nullptr))throw Failure{L"psnProfile"};
        std::string decoded(size,'\0');
        if(!CryptStringToBinaryW(payload.c_str(),static_cast<DWORD>(payload.size()),CRYPT_STRING_BASE64,reinterpret_cast<BYTE*>(decoded.data()),&size,nullptr,nullptr))throw Failure{L"psnProfile"};
        decoded.resize(size);const auto claims=JsonObject::Parse(winrt::to_hstring(decoded));accountId=Str(claims,L"account_id");
        if(accountId.empty()||accountId.size()>20||!std::all_of(accountId.begin(),accountId.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))throw Failure{L"psnProfile"};
        const auto profileReply=Http(L"GET",L"m.np.playstation.com",L"/api/userProfile/v1/internal/users/"+accountId+L"/profiles",{},headers);
        if(profileReply.status==400)throw Failure{L"psnProfile"};
        const auto profile=Json(profileReply);nickname=Str(profile,L"onlineId");
        uint64_t offset=0,total=0;
        do {const auto page=Json(Http(L"GET",L"m.np.playstation.com",L"/api/gamelist/v2/users/me/titles?limit=100&offset="+std::to_wstring(offset)+L"&categories=ps4_game,ps5_native_game",{},headers));if(!page.HasKey(L"titles"))throw Failure{L"invalidData"};const auto titles=Array(page,L"titles");total=Minutes(page,L"totalItemCount").value_or(titles.Size());
            for(const auto& entry:titles){const auto item=entry.GetObject();const auto category=Str(item,L"category");if(category!=L"ps4_game"&&category!=L"ps5_native_game")continue;Game game;game.provider=provider;game.accountId=accountId;game.titleId=Str(item,L"titleId");game.name=Str(item,L"localizedName");game.nameLocale=game.name.empty()?L"en-US":locale;if(game.name.empty())game.name=Str(item,L"name");game.platform=category==L"ps4_game"?L"PS4":L"PS5";game.imageUrl=Str(item,L"localizedImageUrl");if(game.imageUrl.empty())game.imageUrl=Str(item,L"imageUrl");game.artworks.push_back({Str(item,L"imageUrl"),L"gameImage",L""});
                if(item.HasKey(L"concept")){const auto titleConcept=item.GetNamedObject(L"concept");if(titleConcept.HasKey(L"media"))for(const auto& media:Array(titleConcept.GetNamedObject(L"media"),L"images")){const auto image=media.GetObject();game.artworks.push_back({Str(image,L"url"),Str(image,L"type"),L""});}}
                if(item.HasKey(L"media"))game.artworks.push_back({Str(item.GetNamedObject(L"media"),L"screenshotUrl"),L"screenshot",L""});game.firstPlayed=Str(item,L"firstPlayedDateTime");game.lastPlayed=Str(item,L"lastPlayedDateTime");const auto duration=Str(item,L"playDuration");
                if(!duration.empty()){std::wsmatch match;static const std::wregex pattern(LR"(^P(?:(\d+(?:\.\d+)?)D)?(?:T(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?)?$)");if(!std::regex_match(duration,match,pattern)||duration==L"P"||duration==L"PT")throw Failure{L"invalidData"};double minutes=0;constexpr double scale[]={1440,60,1,1.0/60};for(size_t k=0;k<4;++k)if(match[k+1].matched)minutes+=std::stod(match[k+1].str())*scale[k];if(!std::isfinite(minutes)||minutes>9007199254740991.0)throw Failure{L"invalidData"};game.totalMinutes=static_cast<uint64_t>(minutes);}
                ValidateGame(game);games.push_back(std::move(game));}
            offset+=titles.Size();if(!titles.Size())break;
        }while(offset<total);
    }else {
        JsonObject userBody,properties;Put(userBody,L"RelyingParty",L"http://auth.xboxlive.com");Put(userBody,L"TokenType",L"JWT");Put(properties,L"AuthMethod",L"RPS");Put(properties,L"SiteName",L"user.auth.xboxlive.com");Put(properties,L"RpsTicket",L"d="+access);userBody.SetNamedValue(L"Properties",properties);
        const auto user=Json(Http(L"POST",L"user.auth.xboxlive.com",L"/user/authenticate",userBody.Stringify(),L"Content-Type: application/json\r\nx-xbl-contract-version: 1\r\n"));
        JsonObject xstsBody,xstsProperties;Put(xstsBody,L"RelyingParty",L"http://xboxlive.com");Put(xstsBody,L"TokenType",L"JWT");Put(xstsProperties,L"SandboxId",L"RETAIL");JsonArray tokens;tokens.Append(JsonValue::CreateStringValue(winrt::hstring(Str(user,L"Token"))));xstsProperties.SetNamedValue(L"UserTokens",tokens);xstsBody.SetNamedValue(L"Properties",xstsProperties);
        const auto xsts=Json(Http(L"POST",L"xsts.auth.xboxlive.com",L"/xsts/authorize",xstsBody.Stringify(),L"Content-Type: application/json\r\nx-xbl-contract-version: 1\r\n"));
        const auto claims=Array(xsts.GetNamedObject(L"DisplayClaims"),L"xui");if(!claims.Size())throw Failure{L"xboxProfile"};const auto claim=claims.GetObjectAt(0);accountId=Str(claim,L"xid");nickname=Str(claim,L"gtg");if(nickname.empty())nickname=L"Xbox";xboxAuthorization=L"XBL3.0 x="+Str(claim,L"uhs")+L";"+Str(xsts,L"Token");const auto headers=L"Authorization: "+xboxAuthorization+L"\r\nx-xbl-contract-version: 2\r\nAccept-Language: "+locale+L"\r\n";
        const auto history=Json(Http(L"GET",L"titlehub.xboxlive.com",L"/users/xuid("+accountId+L")/titles/titlehistory/decoration/detail,scid,image?maxItems=10000",{},headers));if(!history.HasKey(L"titles"))throw Failure{L"invalidData"};
        std::map<std::wstring,size_t> byScid;
        for(const auto& entry:Array(history,L"titles")){const auto item=entry.GetObject();if(Str(item,L"type")!=L"Game")continue;Game game;game.provider=provider;game.accountId=accountId;game.titleId=Str(item,L"titleId");game.name=Str(item,L"name");game.nameLocale=locale;game.imageUrl=Str(item,L"displayImage");const auto devices=Array(item,L"devices");bool supported=false;for(const auto& value:devices){const auto device=value.GetString();if(device==L"XboxOne"||device==L"XboxSeries")supported=true;}if(!supported)continue;game.platform=L"Xbox One / Series X|S";if(item.HasKey(L"titleHistory"))game.lastPlayed=Str(item.GetNamedObject(L"titleHistory"),L"lastTimePlayed");for(const auto& entry:Array(item,L"images")){const auto image=entry.GetObject();game.artworks.push_back({Str(image,L"url"),Str(image,L"type"),L""});}ValidateGame(game);const auto scid=Str(item,L"serviceConfigId");if(!scid.empty())byScid.emplace(scid,games.size());games.push_back(std::move(game));}
        // Query public statistics in bounded batches, rather than one request per game.
        auto cursor=byScid.begin();
        while(cursor!=byScid.end()){if(m_worker.get_stop_token().stop_requested())return;JsonObject body;JsonArray xuids,groups,stats;xuids.Append(JsonValue::CreateStringValue(winrt::hstring(accountId)));Put(body,L"arrangebyfield",L"xuid");size_t count=0;
            while(cursor!=byScid.end()&&count++<50){JsonObject group,stat;Put(group,L"name",L"Hero");Put(group,L"scid",cursor->first);groups.Append(group);Put(stat,L"name",L"MinutesPlayed");Put(stat,L"scid",cursor->first);stats.Append(stat);++cursor;}
            body.SetNamedValue(L"xuids",xuids);body.SetNamedValue(L"groups",groups);body.SetNamedValue(L"stats",stats);
            const auto reply=Http(L"POST",L"userstats.xboxlive.com",L"/batch",body.Stringify(),headers+L"Content-Type: application/json\r\n");
            if(reply.status==403||reply.status==404)continue;
            const auto values=Json(reply);for(const auto& collection:Array(values,L"statlistscollection"))for(const auto& value:Array(collection.GetObject(),L"stats")){const auto stat=value.GetObject();if(Str(stat,L"name")!=L"MinutesPlayed")continue;const auto found=byScid.find(Str(stat,L"scid"));if(found!=byScid.end())games[found->second].totalMinutes=Minutes(stat,L"value");}
        }
    }
    // Fetch only localized title/image metadata here; playtime and title IDs
    // stay with the primary response in the effective interface language.
    for(const auto language:SupportedLocales){
        if(language==locale)continue;
        if(m_worker.get_stop_token().stop_requested())return;
        const auto addVariant=[&](const JsonObject& item){
            const auto id=Str(item,L"titleId");
            auto device=Str(item,L"platform");if(device.empty())device=Str(item,L"deviceType");
            const bool switch2=device==L"BEE"||device==L"Nintendo Switch 2";
            const auto found=std::find_if(games.begin(),games.end(),[&](const Game& game){return CompareStringOrdinal(id.data(),static_cast<int>(id.size()),game.titleId.data(),static_cast<int>(game.titleId.size()),TRUE)==CSTR_EQUAL&&
                (provider!=Provider::Nintendo||(game.platform==L"Switch 2")==switch2);});
            if(found==games.end())return;
            auto name=Str(item,provider==Provider::Nintendo?L"titleName":provider==Provider::PlayStation?L"localizedName":L"name");
            if(name.empty()&&provider==Provider::PlayStation)name=Str(item,L"name");
            if(!name.empty()&&NitLink::Games::ValidText(name)&&!NitLink::Games::NeedsLocalizedTitle(name,language))found->displayNames[std::wstring(language)]=std::move(name);
            auto image=Str(item,provider==Provider::Nintendo?L"imageUrl":provider==Provider::PlayStation?L"localizedImageUrl":L"displayImage");
            if(image.empty()&&provider==Provider::PlayStation)image=Str(item,L"imageUrl");
            if(image.starts_with(L"http://"))image=L"https://"+image.substr(7);
            if(NitLink::Games::IsAccountArtworkUrl(image)&&std::none_of(found->artworks.begin(),found->artworks.end(),[&](const Artwork& old){return old.url==image;}))
                found->artworks.push_back({std::move(image),L"gameImage",L"",std::wstring(language)});
        };
        try {
            if(provider==Provider::Nintendo){
                const auto headers=L"Authorization: Bearer "+access+L"\r\ngentry-locale: "+std::wstring(language)+L"\r\n";
                const auto history=Json(Http(L"GET",L"app-api.znej.nintendo.com",L"/api/v2.0/users/me/play_histories",{},headers,L"com.nintendo.znej/3.2.0 (iOS/26.0.1)"));
                for(const auto& item:Array(history,L"playHistories"))addVariant(item.GetObject());
            }else if(provider==Provider::PlayStation){
                const auto headers=L"Authorization: Bearer "+access+L"\r\nAccept-Language: "+std::wstring(language==L"zh-TW"?L"zh-Hant-TW":language)+L"\r\n";
                uint64_t offset=0,total=0;
                do {
                    const auto page=Json(Http(L"GET",L"m.np.playstation.com",L"/api/gamelist/v2/users/me/titles?limit=100&offset="+std::to_wstring(offset)+L"&categories=ps4_game,ps5_native_game",{},headers));
                    const auto titles=Array(page,L"titles");total=Minutes(page,L"totalItemCount").value_or(titles.Size());
                    for(const auto& item:titles)addVariant(item.GetObject());
                    offset+=titles.Size();if(!titles.Size())break;
                }while(offset<total);
            }else {
                // The history response is localized; statistics are queried only once above.
                const auto historyHeaders=L"Authorization: "+xboxAuthorization+L"\r\nx-xbl-contract-version: 2\r\nAccept-Language: "+std::wstring(language)+L"\r\n";
                const auto history=Json(Http(L"GET",L"titlehub.xboxlive.com",L"/users/xuid("+accountId+L")/titles/titlehistory/decoration/detail,scid,image?maxItems=10000",{},historyHeaders));
                for(const auto& item:Array(history,L"titles"))addVariant(item.GetObject());
            }
        }catch(const Failure&){continue;}catch(const winrt::hresult_error&){continue;}
    }
    for(auto& game:games)if(game.imageUrl.empty()&&!game.artworks.empty())game.imageUrl=game.artworks.front().url;
    if(accountId.empty()||!NitLink::Games::ValidText(nickname,256))throw Failure{L"invalidData"};
    std::stable_sort(games.begin(),games.end(),[](const auto& a,const auto& b){return a.lastPlayed>b.lastPlayed;});
    {std::lock_guard lock(m_mutex);auto& state=m_states[Index(provider)];state.games=std::move(games);state.accountId=accountId;state.nickname=nickname;state.syncedAt=Now();state.status=L"connected";state.error.clear();}
    Save();m_changed=true;
}
} // namespace NitLink::Accounts
