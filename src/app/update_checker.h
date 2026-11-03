#pragma once

#include "holybear_version.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

namespace NitLink::HolyBear {
inline constexpr size_t MaxResponseBytes = 256 * 1024;
bool AutoCheckDue(bool enabled, bool busy, uint64_t nowUtc, uint64_t lastCheckUtc);
enum class UpdateStatus { Never, Checking, Current, Available, Offline, RateLimited, NotFound,
    ServerUnavailable, ApiUnavailable, Invalid, Cancelled };
const wchar_t* StatusKey(UpdateStatus status);
struct HttpReply {
    uint32_t status = 0, error = 0;
    std::string body;
    std::optional<uint32_t> rateLimitRemaining;
};
std::optional<uint32_t> ParseRateLimitRemaining(std::string_view value);
struct InstallerAsset {
    std::wstring tag, name, url;
    std::string sha256;
    uint64_t bytes = 0;
};
struct UpdateResult {
    UpdateStatus status = UpdateStatus::Invalid;
    std::wstring tag, notes, publishedAt;
    std::optional<InstallerAsset> installer;
};
UpdateResult ClassifyReply(const HttpReply& reply, std::wstring_view current = Version);
bool IsUpdateDownloadRedirect(std::wstring_view url);

enum class TransferStatus { Idle, Downloading, Verifying, Ready, Installing, Cancelled,
    NetworkError, DiskError, Corrupt, LaunchError };
const wchar_t* StatusKey(TransferStatus status);
struct TransferState {
    TransferStatus status = TransferStatus::Idle;
    std::wstring tag;
    uint64_t received = 0, total = 0;
};

class UpdateInstaller {
public:
    using Sink = std::function<bool(std::string_view)>;
    using Transport = std::function<uint32_t(const InstallerAsset&, std::stop_token, const Sink&)>;
    using Launcher = std::function<bool(const std::filesystem::path&, const std::filesystem::path&)>;
    explicit UpdateInstaller(std::filesystem::path directory = {}, Transport transport = {}, Launcher launcher = {});
    ~UpdateInstaller();
    bool Start(const InstallerAsset& asset);
    void Cancel();
    bool Busy() const { return m_busy.load(); }
    std::optional<TransferState> TakeState();
    bool Install(const std::filesystem::path& applicationDirectory);
private:
    void Publish(TransferStatus status, uint64_t received = 0);
    std::filesystem::path m_directory, m_readyPath;
    Transport m_transport;
    Launcher m_launcher;
    std::jthread m_worker;
    std::atomic<bool> m_busy{false}, m_changed{false};
    std::mutex m_mutex;
    TransferState m_state;
};

// Worker owns all HTTP/JSON work. UI/render thread only reads a completion flag
// and consumes a bounded result; it never waits for a network operation.
class UpdateChecker {
public:
    using Transport = std::function<HttpReply(std::stop_token)>;
    explicit UpdateChecker(Transport transport = {});
    ~UpdateChecker();
    bool Start();
    void Cancel();
    bool Busy() const { return m_busy.load(); }
    std::optional<UpdateResult> TakeResult();
private:
    Transport m_transport;
    std::jthread m_worker;
    std::atomic<bool> m_busy{false}, m_ready{false};
    std::mutex m_mutex;
    UpdateResult m_result;
};
} // namespace NitLink::HolyBear
