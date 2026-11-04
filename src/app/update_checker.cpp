#include "update_checker.h"
#include <windows.h>
#include <winhttp.h>
#include <roapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <chrono>
#include <charconv>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>

namespace NitLink::HolyBear {
bool AutoCheckDue(bool enabled, bool busy, uint64_t now, uint64_t last) {
    return enabled && !busy && now && (!last || (now >= last && now - last >= 86400));
}
std::optional<uint32_t> ParseRateLimitRemaining(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
        value.front() == '\r' || value.front() == '\n')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
        value.back() == '\r' || value.back() == '\n')) value.remove_suffix(1);
    if (value.empty()) return {};
    uint32_t remaining = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), remaining);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return {};
    return remaining;
}
const wchar_t* StatusKey(UpdateStatus status) {
    switch (status) {
    case UpdateStatus::Checking: return L"checking";
    case UpdateStatus::Current: return L"current";
    case UpdateStatus::Available: return L"available";
    case UpdateStatus::Offline: return L"offline";
    case UpdateStatus::RateLimited: return L"rateLimited";
    case UpdateStatus::NotFound: return L"notFound";
    case UpdateStatus::ServerUnavailable: return L"serverUnavailable";
    case UpdateStatus::ApiUnavailable: return L"apiUnavailable";
    case UpdateStatus::Invalid: return L"invalid";
    default: return L"never";
    }
}
UpdateResult ClassifyReply(const HttpReply& reply, std::wstring_view current) {
    if (reply.error == ERROR_WINHTTP_OPERATION_CANCELLED) return {UpdateStatus::Cancelled, {}};
    if (reply.error == ERROR_FILE_TOO_LARGE) return {};
    if (reply.error || !reply.status) return {UpdateStatus::Offline, {}};
    if (reply.status == 403)
        return {reply.rateLimitRemaining == 0 ? UpdateStatus::RateLimited : UpdateStatus::ApiUnavailable, {}};
    if (reply.status == 404) return {UpdateStatus::NotFound, {}};
    if (reply.status == 429) return {UpdateStatus::RateLimited, {}};
    if (reply.status >= 500 && reply.status <= 599) return {UpdateStatus::ServerUnavailable, {}};
    if (reply.status != 200) return {UpdateStatus::ApiUnavailable, {}};
    if (reply.body.empty() || reply.body.size() > MaxResponseBytes || !ParseVersion(current)) return {};
    // Use Windows' JSON implementation, accepting full GitHub objects/arrays but
    // reading only strictly typed release identity fields. No response HTML is used.
    const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return {};
    struct Apartment {
        bool close;
        ~Apartment() {
            if (close) {
                // This executable has no WinRT DllCanUnloadNow hook. Cached
                // factories must be released before COM can unload their DLLs,
                // including between two checks on short-lived worker threads.
                winrt::clear_factory_cache();
                RoUninitialize();
            }
        }
    } apartment{SUCCEEDED(initialized)};
    try {
        using namespace winrt::Windows::Data::Json;
        JsonObject json;
        if (!JsonObject::TryParse(winrt::to_hstring(reply.body), json)) return {};
        for (const auto key : {L"draft", L"prerelease"}) {
            const auto field = json.GetNamedValue(key);
            if (field.ValueType() != JsonValueType::Boolean || field.GetBoolean()) return {};
        }
        if (json.GetNamedValue(L"tag_name").ValueType() != JsonValueType::String ||
            json.GetNamedValue(L"html_url").ValueType() != JsonValueType::String) return {};
        const std::wstring tag(json.GetNamedString(L"tag_name"));
        const std::wstring url(json.GetNamedString(L"html_url"));
        const auto version = ParseVersion(tag);
        if (!version || !tag.starts_with(L"v") || url != std::wstring(ReleasePrefix) + tag) return {};
        UpdateResult result{*version > *ParseVersion(current) ? UpdateStatus::Available : UpdateStatus::Current, tag};
        const auto notes = json.GetNamedValue(L"body", JsonValue::CreateNullValue());
        if (notes.ValueType() == JsonValueType::String) result.notes = std::wstring(notes.GetString()).substr(0, 65536);
        else if (notes.ValueType() != JsonValueType::Null) return {};
        const auto published = json.GetNamedValue(L"published_at", JsonValue::CreateNullValue());
        if (published.ValueType() == JsonValueType::String) result.publishedAt = std::wstring(published.GetString()).substr(0, 64);
        const auto assets = json.GetNamedValue(L"assets", JsonValue::CreateNullValue());
        const auto numeric = tag.substr(1, tag.size() - 10);
        const std::wstring name = L"NitLink-" + numeric + L"-HolyBear-Setup-x64.exe";
        if (assets.ValueType() == JsonValueType::Array) for (const auto& item : assets.GetArray()) {
            if (item.ValueType() != JsonValueType::Object) continue;
            const auto asset = item.GetObject();
            const auto assetName = asset.GetNamedValue(L"name", JsonValue::CreateNullValue());
            if (assetName.ValueType() != JsonValueType::String || assetName.GetString() != name) continue;
            const auto url = asset.GetNamedValue(L"browser_download_url");
            const auto digest = asset.GetNamedValue(L"digest");
            const auto size = asset.GetNamedValue(L"size");
            if (url.ValueType() != JsonValueType::String || digest.ValueType() != JsonValueType::String ||
                size.ValueType() != JsonValueType::Number) return {};
            const std::wstring expected = L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/download/" + tag + L"/" + name;
            const std::wstring hash(digest.GetString());
            const double bytes = size.GetNumber();
            if (url.GetString() != expected || !hash.starts_with(L"sha256:") || hash.size() != 71 ||
                !std::all_of(hash.begin() + 7, hash.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); }) ||
                !std::isfinite(bytes) || bytes < 1 || bytes > 200 * 1024 * 1024 || std::floor(bytes) != bytes || result.installer) return {};
            result.installer = InstallerAsset{tag, name, expected, std::string(hash.begin() + 7, hash.end()), static_cast<uint64_t>(bytes)};
        }
        return result;
    } catch (...) { return {}; }
}

namespace {
using Clock = std::chrono::steady_clock;
struct HttpHandle {
    HINTERNET value = nullptr;
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};
struct Progress {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD error = 0, bytes = 0;
    ~Progress() { if (event) CloseHandle(event); if (closed) CloseHandle(closed); }
};
void CALLBACK HttpCallback(HINTERNET, DWORD_PTR context, DWORD status, void* data, DWORD length) {
    auto* progress = reinterpret_cast<Progress*>(context);
    if (!progress) return;
    switch (status) {
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING: SetEvent(progress->closed); return;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        progress->error = static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError; break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: progress->bytes = length; break;
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: break;
    default: return;
    }
    SetEvent(progress->event);
}
// Close only asynchronous handles. HANDLE_CLOSING is the final callback; wait
// for it on the worker before destroying its callback context (never on Present).
struct AsyncRequest {
    HINTERNET value = nullptr;
    Progress& progress;
    bool callbackReady = false;
    ~AsyncRequest() {
        if (value) {
            WinHttpCloseHandle(value);
            if (callbackReady) WaitForSingleObject(progress.closed, INFINITE);
        }
    }
};
std::optional<uint32_t> ReadRateLimitRemaining(HINTERNET request) {
    wchar_t header[32]{};
    wchar_t headerName[] = L"X-RateLimit-Remaining";
    DWORD bytes = 0;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, headerName, nullptr, &bytes,
            WINHTTP_NO_HEADER_INDEX) || GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        !bytes || bytes > sizeof(header)) return {};
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, headerName, header, &bytes,
        WINHTTP_NO_HEADER_INDEX)) return {};
    size_t length = bytes / sizeof(wchar_t);
    while (length && header[length - 1] == L'\0') --length;
    std::string value;
    value.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        if (header[i] > 0x7f) return {};
        value.push_back(static_cast<char>(header[i]));
    }
    return ParseRateLimitRemaining(value);
}
HttpReply FetchHttp(std::wstring url, std::stop_token stop, uint64_t limit,
                    std::chrono::seconds budget, bool redirects, const UpdateInstaller::Sink& sink,
                    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n") {
    HttpReply reply;
    const auto deadline = Clock::now() + budget;
    HttpHandle session{WinHttpOpen(L"NitLink-HolyBear-UpdateCheck/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
    if (!session.value) { reply.error = GetLastError(); return reply; }
    if (!WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 5000)) { reply.error = GetLastError(); return reply; }
    for (unsigned hop = 0; hop < 6; ++hop) {
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        reply.error = ERROR_WINHTTP_INVALID_URL; return reply;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
        (parts.dwExtraInfoLength ? std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength) : L"");
    Progress progress;
    if (!progress.event || !progress.closed) { reply.error = ERROR_NOT_ENOUGH_MEMORY; return reply; }
    HttpHandle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    if (!connection.value) { reply.error = GetLastError(); return reply; }
    AsyncRequest request{WinHttpOpenRequest(connection.value, L"GET",
        path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), progress};
    if (!request.value) { reply.error = GetLastError(); return reply; }
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&progress);
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
        reply.error = GetLastError(); return reply;
    }
    if (WinHttpSetStatusCallback(request.value, HttpCallback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
            WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        reply.error = GetLastError(); return reply;
    }
    request.callbackReady = true;
    auto await = [&]() {
        while (!stop.stop_requested() && Clock::now() < deadline) {
            if (WaitForSingleObject(progress.event, 50) == WAIT_OBJECT_0) {
                reply.error = progress.error; return !reply.error;
            }
        }
        reply.error = stop.stop_requested() ? ERROR_WINHTTP_OPERATION_CANCELLED : ERROR_WINHTTP_TIMEOUT;
        return false;
    };
    if (stop.stop_requested()) { reply.error = ERROR_WINHTTP_OPERATION_CANCELLED; return reply; }
    if (!WinHttpSendRequest(request.value, headers, -1,
            WINHTTP_NO_REQUEST_DATA, 0, 0, context) || !await() ||
        !WinHttpReceiveResponse(request.value, nullptr) || !await()) {
        if (!reply.error) reply.error = GetLastError(); return reply;
    }
    DWORD size = sizeof(reply.status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &reply.status, &size, WINHTTP_NO_HEADER_INDEX)) {
        reply.error = GetLastError(); return reply;
    }
    if (reply.status == 403) reply.rateLimitRemaining = ReadRateLimitRemaining(request.value);
    if (reply.status == 301 || reply.status == 302 || reply.status == 303 || reply.status == 307 || reply.status == 308) {
        wchar_t location[8192]{};
        DWORD length = sizeof(location);
        if (!redirects || !WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                location, &length, WINHTTP_NO_HEADER_INDEX) || !IsUpdateDownloadRedirect(location)) {
            reply.error = ERROR_WINHTTP_REDIRECT_FAILED; return reply;
        }
        url = location;
        continue;
    }
    if (reply.status != 200) return reply;
    uint64_t received = 0;
    char buffer[65536];
    while (!stop.stop_requested()) {
        progress.bytes = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), nullptr) || !await()) {
            if (!reply.error) reply.error = GetLastError(); return reply;
        }
        if (!progress.bytes) return reply;
        if (progress.bytes > limit - received) {
            reply.error = ERROR_FILE_TOO_LARGE; return reply;
        }
        if (!sink(std::string_view(buffer, progress.bytes))) { reply.error = ERROR_WRITE_FAULT; return reply; }
        received += progress.bytes;
    }
    reply.error = ERROR_WINHTTP_OPERATION_CANCELLED;
    return reply;
    }
    reply.error = ERROR_WINHTTP_REDIRECT_FAILED;
    return reply;
}
HttpReply FetchRelease(std::stop_token stop) {
    std::string body;
    auto reply = FetchHttp(L"https://api.github.com" + std::wstring(ReleaseApiPath), stop,
        MaxResponseBytes, std::chrono::seconds(15), false,
        [&](std::string_view bytes) { body.append(bytes); return true; });
    reply.body = std::move(body);
    return reply;
}
} // namespace

AboutDocumentLoader::AboutDocumentLoader(Transport transport) : m_transport(std::move(transport)) {
    if (!m_transport) m_transport = [](const AboutDocumentSource& source, std::stop_token stop) {
        std::string body;
        auto reply = FetchHttp(std::wstring(source.url), stop, MaxResponseBytes, std::chrono::seconds(15), false,
            [&](std::string_view bytes) { body.append(bytes); return true; },
            L"Accept: text/plain\r\nCache-Control: no-cache\r\n");
        reply.body = std::move(body);
        return reply;
    };
}
AboutDocumentLoader::~AboutDocumentLoader() { Cancel(); if (m_worker.joinable()) m_worker.join(); }
bool AboutDocumentLoader::Start(const AboutDocumentSource& source) {
    if (m_busy.exchange(true)) return false;
    if (m_worker.joinable()) m_worker.join();
    m_ready = false;
    m_worker = std::jthread([this, source](std::stop_token stop) {
        AboutDocumentResult result{std::wstring(source.id), {}};
        const auto reply = m_transport(source, stop);
        if (!reply.error && reply.status == 200 && !reply.body.empty() && reply.body.size() <= MaxResponseBytes) {
            const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reply.body.data(),
                static_cast<int>(reply.body.size()), nullptr, 0);
            if (length) {
                result.markdown.resize(length);
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reply.body.data(),
                    static_cast<int>(reply.body.size()), result.markdown.data(), length);
            }
        }
        if (!stop.stop_requested()) {
            { std::lock_guard lock(m_mutex); m_result = std::move(result); }
            m_ready = true;
        }
        m_busy = false;
    });
    return true;
}
void AboutDocumentLoader::Cancel() { if (m_worker.joinable()) m_worker.request_stop(); }
std::optional<AboutDocumentResult> AboutDocumentLoader::TakeResult() {
    if (!m_ready.load(std::memory_order_relaxed) || !m_ready.exchange(false)) return {};
    std::lock_guard lock(m_mutex);
    return m_result;
}

bool IsUpdateDownloadRedirect(std::wstring_view url) {
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (url.size() > 8192 || url.find(L'#') != url.npos ||
        !WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
        parts.dwUserNameLength || parts.dwPasswordLength) return false;
    const std::wstring_view host(parts.lpszHostName, parts.dwHostNameLength);
    return host == L"github.com" || host == L"api.github.com" ||
        host == L"release-assets.githubusercontent.com" || host == L"objects.githubusercontent.com";
}

const wchar_t* StatusKey(TransferStatus status) {
    switch (status) {
    case TransferStatus::Downloading: return L"downloading";
    case TransferStatus::Verifying: return L"verifying";
    case TransferStatus::Ready: return L"ready";
    case TransferStatus::Installing: return L"installing";
    case TransferStatus::Cancelled: return L"cancelled";
    case TransferStatus::NetworkError: return L"downloadError";
    case TransferStatus::DiskError: return L"diskError";
    case TransferStatus::Corrupt: return L"corrupt";
    case TransferStatus::LaunchError: return L"launchError";
    default: return L"idle";
    }
}

UpdateInstaller::UpdateInstaller(std::filesystem::path directory, Transport transport, Launcher launcher)
    : m_directory(std::move(directory)), m_transport(std::move(transport)), m_launcher(std::move(launcher)) {
    if (m_directory.empty()) {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
            m_directory = std::filesystem::path(local) / L"NitLink" / L"Updates";
            CoTaskMemFree(local);
        }
    }
    if (!m_transport) m_transport = [](const InstallerAsset& asset, std::stop_token stop, const Sink& sink) {
        const auto reply = FetchHttp(asset.url, stop, asset.bytes, std::chrono::seconds(600), true, sink);
        return reply.error ? reply.error : reply.status == 200 ? 0u : static_cast<uint32_t>(ERROR_WINHTTP_INVALID_SERVER_RESPONSE);
    };
    if (!m_launcher) m_launcher = [](const std::filesystem::path& setup, const std::filesystem::path& target) {
        const std::wstring arguments = L"--auto-update /WAITPID=" + std::to_wstring(GetCurrentProcessId()) + L" /D=" + target.wstring();
        SHELLEXECUTEINFOW launch{sizeof(launch)};
        launch.fMask = SEE_MASK_NOCLOSEPROCESS;
        launch.lpVerb = L"open";
        launch.lpFile = setup.c_str();
        launch.lpParameters = arguments.c_str();
        const auto directory = setup.parent_path().wstring();
        launch.lpDirectory = directory.c_str();
        launch.nShow = SW_SHOWNORMAL;
        const bool started = ShellExecuteExW(&launch) != FALSE;
        if (launch.hProcess) CloseHandle(launch.hProcess);
        return started;
    };
}
UpdateInstaller::~UpdateInstaller() { Cancel(); if (m_worker.joinable()) m_worker.join(); }
void UpdateInstaller::Cancel() { if (m_worker.joinable()) m_worker.request_stop(); }
void UpdateInstaller::Publish(TransferStatus status, uint64_t received) {
    { std::lock_guard lock(m_mutex); m_state.status = status; m_state.received = received; }
    m_changed = true;
}
std::optional<TransferState> UpdateInstaller::TakeState() {
    if (!m_changed.exchange(false)) return {};
    std::lock_guard lock(m_mutex);
    return m_state;
}
bool UpdateInstaller::Start(const InstallerAsset& asset) {
    if (m_busy.exchange(true)) return false;
    if (m_worker.joinable()) m_worker.join();
    { std::lock_guard lock(m_mutex); m_state = {TransferStatus::Downloading, asset.tag, 0, asset.bytes}; m_readyPath.clear(); }
    m_changed = true;
    if (m_directory.empty()) { Publish(TransferStatus::DiskError); m_busy = false; return false; }
    m_worker = std::jthread([this, asset](std::stop_token stop) {
        const auto target = m_directory / asset.name;
        const auto partial = std::filesystem::path(target.wstring() + L".partial");
        TransferStatus outcome = TransferStatus::DiskError;
        uint64_t received = 0;
        try {
            std::error_code error;
            std::filesystem::create_directories(m_directory, error);
            std::ofstream output;
            if (!error) output.open(partial, std::ios::binary | std::ios::trunc);
            struct Hash { BCRYPT_HASH_HANDLE value = nullptr; ~Hash() { if (value) BCryptDestroyHash(value); } } hash;
            if (output && BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash.value, nullptr, 0, nullptr, 0, 0) >= 0) {
                auto notified = Clock::now();
                const auto failure = m_transport(asset, stop, [&](std::string_view bytes) {
                    if (bytes.size() > asset.bytes - received) { outcome = TransferStatus::Corrupt; return false; }
                    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                    if (!output || BCryptHashData(hash.value, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0) < 0) return false;
                    received += bytes.size();
                    if (received == asset.bytes || Clock::now() - notified >= std::chrono::milliseconds(200)) {
                        notified = Clock::now(); Publish(TransferStatus::Downloading, received);
                    }
                    return !stop.stop_requested();
                });
                output.close();
                if (stop.stop_requested()) outcome = TransferStatus::Cancelled;
                else if (failure) {
                    if (failure == ERROR_FILE_TOO_LARGE) outcome = TransferStatus::Corrupt;
                    else if (failure != ERROR_WRITE_FAULT && outcome != TransferStatus::Corrupt) outcome = TransferStatus::NetworkError;
                } else if (output) {
                    Publish(TransferStatus::Verifying, received);
                    unsigned char digest[32]{};
                    std::string actual;
                    if (BCryptFinishHash(hash.value, digest, sizeof(digest), 0) >= 0) {
                        constexpr char hex[] = "0123456789abcdef";
                        for (const auto byte : digest) { actual += hex[byte >> 4]; actual += hex[byte & 15]; }
                    }
                    if (received != asset.bytes || actual != asset.sha256) outcome = TransferStatus::Corrupt;
                    else if (MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                        { std::lock_guard lock(m_mutex); m_readyPath = target; }
                        outcome = TransferStatus::Ready;
                    }
                }
            }
        } catch (const std::exception&) { outcome = TransferStatus::DiskError; }
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        Publish(outcome, received);
        m_busy = false;
    });
    return true;
}
bool UpdateInstaller::Install(const std::filesystem::path& applicationDirectory) {
    std::filesystem::path setup;
    { std::lock_guard lock(m_mutex);
      if (m_state.status != TransferStatus::Ready && m_state.status != TransferStatus::LaunchError) return false;
      setup = m_readyPath; m_state.status = TransferStatus::Installing; }
    m_changed = true;
    if (m_launcher(setup, applicationDirectory)) return true;
    Publish(TransferStatus::LaunchError);
    return false;
}

UpdateChecker::UpdateChecker(Transport transport) : m_transport(transport ? std::move(transport) : FetchRelease) {}
UpdateChecker::~UpdateChecker() { Cancel(); if (m_worker.joinable()) m_worker.join(); }
bool UpdateChecker::Start() {
    if (m_busy.exchange(true)) return false;
    if (m_worker.joinable()) m_worker.join(); // Previous worker is already finished.
    m_ready = false;
    try {
        m_worker = std::jthread([this](std::stop_token stop) {
            UpdateResult result;
            try { result = ClassifyReply(m_transport(stop)); } catch (...) { result.status = UpdateStatus::Offline; }
            if (stop.stop_requested()) result = {UpdateStatus::Cancelled, {}};
            { std::lock_guard lock(m_mutex); m_result = std::move(result); }
            m_ready = true;
            m_busy = false;
        });
    } catch (...) { m_busy = false; return false; }
    return true;
}
void UpdateChecker::Cancel() { if (m_worker.joinable()) m_worker.request_stop(); }
std::optional<UpdateResult> UpdateChecker::TakeResult() {
    if (!m_ready.load(std::memory_order_relaxed) || !m_ready.exchange(false)) return {};
    std::lock_guard lock(m_mutex);
    return m_result;
}
} // namespace NitLink::HolyBear
