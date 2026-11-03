#include "app/update_checker.h"
#include <windows.h>
#include <winhttp.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>
#include <filesystem>
#include <fstream>
using namespace NitLink::HolyBear;
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    // Explicit post-publication probe; CTest always uses offline mock responses.
    if (argc == 2 && (std::string_view(argv[1]) == "--live-release" || std::string_view(argv[1]) == "--live-download")) {
        UpdateChecker live;
        if (!live.Start()) return 1;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (live.Busy() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto result = live.TakeResult();
        if (!result) return 1;
        std::wcout << L"Live HolyBear Release: " << StatusKey(result->status) << L"; " << result->tag << L'\n';
        if (std::string_view(argv[1]) == "--live-download") {
            if (!result->installer) return 1;
            const auto directory = std::filesystem::temp_directory_path() /
                ("NitLink-Live-Update-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
            UpdateInstaller download(directory);
            if (!download.Start(*result->installer)) return 1;
            const auto until = std::chrono::steady_clock::now() + std::chrono::minutes(11);
            while (download.Busy() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            if (download.Busy()) { download.Cancel(); return 1; }
            const auto state = download.TakeState();
            std::wcout << L"Live installer transfer: " << (state ? StatusKey(state->status) : L"missing") << L"; "
                << (state ? state->received : 0) << L" bytes; installer NOT launched\n";
            std::error_code ignored;
            std::filesystem::remove(directory / result->installer->name, ignored);
            std::filesystem::remove(directory / (result->installer->name + L".partial"), ignored);
            std::filesystem::remove(directory, ignored);
            return state && state->status == TransferStatus::Ready ? 0 : 1;
        }
        return result->status == UpdateStatus::Current && result->tag == L"v" + std::wstring(Version) ? 0 : 1;
    }
    unsigned checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) { ++checks; if (!ok) ++failures; std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; };
    check(ParseVersion(L"v1.2.4-holybear") == ParseVersion(L"1.2.4.0-holybear"), "three/four part equality");
    check(ParseVersion(L"v1.2.4.1-holybear") > ParseVersion(L"1.2.4-holybear"), "1.2.4.1 update from published 1.2.4");
    check(ParseVersion(L"v1.2.4.2-holybear") > ParseVersion(L"v1.2.4.1-holybear"), "numeric revision");
    check(ParseVersion(L"v1.2.4.10-holybear") > ParseVersion(L"v1.2.4.2-holybear"), "numeric, not lexical order");
    check(ParseVersion(L"v1.2.5-holybear") > ParseVersion(L"v1.2.4.2-holybear"), "patch exceeds revision");
    check(ParseVersion(L"v1.2.5.1-holybear") > ParseVersion(L"v1.2.5-holybear"), "1.2.5.1 update exceeds 1.2.5");
    check(ParseVersion(L"v1.2.5.2-holybear") > ParseVersion(L"v1.2.5.1-holybear"), "1.2.5.2 update exceeds 1.2.5.1");
    check(ParseVersion(L"v1.2.5.3-holybear") > ParseVersion(L"v1.2.5.2-holybear"), "1.2.5.3 update exceeds 1.2.5.2");
    for (auto bad : {L"1.2.4", L"v0.1.2", L"v1.2.4-holybear-beta", L"v1.2-holybear", L"v1.02.4-holybear",
            L"v1.2.4.1.1-holybear", L"v65536.2.4-holybear", L"v1.2.4.-holybear", L"v1.2.4-holybear/evil"})
        check(!ParseVersion(bad), "invalid/other edition version");
    auto response = [](std::string tag) {
        return HttpReply{200, 0, "{\"tag_name\":\"" + tag + "\",\"draft\":false,\"prerelease\":false,\"html_url\":\"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/" + tag + "\",\"assets\":[],\"body\":null}"};
    };
    std::string currentTag = "v";
    for (auto c : Version) currentTag.push_back(static_cast<char>(c));
    check(ClassifyReply(response(currentTag)).status == UpdateStatus::Current, "equal current revision");
    check(ClassifyReply(response("v1.2.5-holybear"), L"1.2.4.1-holybear").status == UpdateStatus::Available,
        "published 1.2.4.1 upgrades to 1.2.5");
    check(ClassifyReply(response("v1.2.5.1-holybear"), L"1.2.5-holybear").status == UpdateStatus::Available,
        "published 1.2.5.1 upgrades to 1.2.5");
    const auto release1251 = ClassifyReply(response("v1.2.5.1-holybear"), L"1.2.5-holybear");
    check(release1251.tag == L"v1.2.5.1-holybear" &&
        std::wstring(ReleasePrefix) + release1251.tag == L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5.1-holybear" &&
        IsReleaseUrl(std::wstring(ReleasePrefix) + release1251.tag), "1.2.5.1 uses the exact canonical release URL");
    check(ClassifyReply(response("v1.2.4.1-holybear"), L"1.2.4-holybear").status == UpdateStatus::Available,
        "published 1.2.4 recognizes new revision tag");
    const auto revision = ClassifyReply(response("v1.2.4.1-holybear"), L"1.2.4-holybear");
    check(revision.tag == L"v1.2.4.1-holybear" &&
        std::wstring(ReleasePrefix) + revision.tag == L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.1-holybear" &&
        IsReleaseUrl(std::wstring(ReleasePrefix) + revision.tag), "revision update retains the exact official v-tag URL");
    const std::pair<const char*, const char*> invalidRevisionFields[] = {
        {"\"draft\":false", "\"draft\":true"},
        {"\"draft\":false", "\"draft\":\"false\""},
        {"\"draft\":false", "\"draft\":0"},
        {"\"prerelease\":false", "\"prerelease\":true"},
        {"\"prerelease\":false", "\"prerelease\":null"},
        {"\"tag_name\":\"v1.2.4.1-holybear\"", "\"tag_name\":42"},
        {"\"tag_name\":\"v1.2.4.1-holybear\"", "\"tag_name\":null"},
        {"\"tag_name\":\"v1.2.4.1-holybear\"", "\"tag_name\":\"1.2.4.1-holybear\""},
        {"\"tag_name\":\"v1.2.4.1-holybear\"", "\"tag_name\":\"v1.2.4.1-holybear-beta\""},
        {"\"tag_name\":\"v1.2.4.1-holybear\"", "\"tag_name\":\"v1.2.4.2-holybear\""},
        {"\"html_url\":\"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.1-holybear\"", "\"html_url\":false"},
        {"\"html_url\":\"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.1-holybear\"", "\"html_url\":42"},
        {"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.1-holybear", "https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/1.2.4.1-holybear"},
        {"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.1-holybear", "https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.1-holybear?redirect=evil"},
        {"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.1-holybear", "https://github.com/CN115/nitlink/releases/tag/v1.2.4.1-holybear"}
    };
    for (const auto& [field, replacement] : invalidRevisionFields) {
        auto reply = response("v1.2.4.1-holybear");
        reply.body.replace(reply.body.find(field), std::string_view(field).size(), replacement);
        check(ClassifyReply(reply, L"1.2.4-holybear").status == UpdateStatus::Invalid,
            "revision update rejects nonformal, mismatched or incorrectly typed release data");
    }
    check(ClassifyReply(response("v1.2.6-holybear")).status == UpdateStatus::Available, "newer version");
    check(ClassifyReply(response("v1.2.3-holybear")).status == UpdateStatus::Current, "local newer version");
    check(ClassifyReply(response("v1.2.4.2-holybear"), L"1.2.4.1-holybear").status == UpdateStatus::Available, "revision API release");
    auto invalid = response("v1.2.5-holybear");
    auto pos = invalid.body.find("\"prerelease\":false"); invalid.body.replace(pos, 18, "\"prerelease\":true");
    check(ClassifyReply(invalid).status == UpdateStatus::Invalid, "prerelease rejected");
    for (auto json : {"{", "{}", "{\"tag_name\":42}", "{\"tag_name\":\"v1.2.5-holybear\",\"draft\":false,\"prerelease\":\"false\"}"})
        check(ClassifyReply({200, 0, json}).status == UpdateStatus::Invalid, "malformed or missing typed JSON fields");
    invalid = response("v1.2.5-holybear"); pos = invalid.body.find("HolyBearTW"); invalid.body.replace(pos, 10, "nitlink-dev");
    check(ClassifyReply(invalid).status == UpdateStatus::Invalid, "other repository rejected");
    check(ClassifyReply({200, 0, std::string(MaxResponseBytes + 1, 'x')}).status == UpdateStatus::Invalid, "response size bound");
    check(ClassifyReply({0, ERROR_WINHTTP_TIMEOUT, {}}).status == UpdateStatus::Offline, "timeout nonfatal");
    check(ClassifyReply({0, ERROR_WINHTTP_CANNOT_CONNECT, {}}).status == UpdateStatus::Offline, "offline nonfatal");
    check(ClassifyReply({403, 0, "{}", 0}).status == UpdateStatus::RateLimited, "403 with zero remaining is rate-limited");
    check(ClassifyReply({403, 0, "{}", 28}).status == UpdateStatus::ApiUnavailable, "403 with remaining quota is not called rate-limited");
    check(ClassifyReply({403, 0, "{}"}).status == UpdateStatus::ApiUnavailable, "403 without quota evidence is not called rate-limited");
    check(ClassifyReply({429, 0, "{}"}).status == UpdateStatus::RateLimited, "429 temporarily rate-limited");
    check(ClassifyReply({404, 0, "{}"}).status == UpdateStatus::NotFound, "404 means no release endpoint found");
    for (auto code : {500u, 502u, 503u, 599u})
        check(ClassifyReply({code, 0, "{}"}).status == UpdateStatus::ServerUnavailable, "5xx server unavailable");
    for (auto code : {301u, 302u, 400u, 401u, 418u})
        check(ClassifyReply({code, 0, "{}"}).status == UpdateStatus::ApiUnavailable, "other HTTP response nonfatal");
    check(ParseRateLimitRemaining("0") == 0, "rate limit header parses zero");
    check(ParseRateLimitRemaining(" 28\r\n") == 28, "rate limit header trims HTTP whitespace");
    for (auto value : {"", "-1", "28, 30", "4294967296", "1x"})
        check(!ParseRateLimitRemaining(value), "malformed rate limit header rejected");
    check(std::wstring_view(StatusKey(UpdateStatus::RateLimited)) == L"rateLimited" &&
        std::wstring_view(StatusKey(UpdateStatus::NotFound)) == L"notFound" &&
        std::wstring_view(StatusKey(UpdateStatus::ServerUnavailable)) == L"serverUnavailable",
        "HTTP categories map to localized UI states");
    check(!AutoCheckDue(false, false, 200000, 0), "automatic opt-out disables checks");
    check(AutoCheckDue(true, false, 200000, 0), "first enabled check");
    check(!AutoCheckDue(true, true, 200000, 0), "busy request deduplication");
    check(!AutoCheckDue(true, false, 186399, 100000), "under 24 hours");
    check(AutoCheckDue(true, false, 186400, 100000), "exact 24 hours");
    check(!AutoCheckDue(false, false, 300000, 100000), "disabled even when overdue");
    check(!AutoCheckDue(true, false, 1, UINT64_MAX), "clock rollback and no unsigned underflow");
    check(ReleaseApiPath == L"/repos/HolyBearTW/nitlink-holybear-edition/releases/latest", "fixed canonical Releases API endpoint");
    check(IsReleaseUrl(L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.4.2-holybear"), "validated release destination");
    for (auto bad : {L"https://github.com/HolyBearTW/nitlink/releases/tag/v1.2.5-holybear",
            L"https://github.com/nitlink-dev/nitlink/releases/tag/v1.2.5-holybear", L"https://github.com/CN115/nitlink/releases/tag/v1.2.5-holybear",
            L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5-holybear?redirect=evil",
            L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5-holybear#fragment",
            L"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/tag/v1.2.5-holybear/extra",
            L"https://github.com@evil/nitlink", L"file:///run.exe"})
        check(!IsReleaseUrl(bad), "unsafe release link rejected");
    std::atomic<unsigned> requests{0}; std::atomic<bool> release{false};
    UpdateChecker worker([&](std::stop_token stop) {
        ++requests;
        while (!release && !stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return response("v1.2.6-holybear");
    });
    check(worker.Start(), "background start");
    check(!worker.Start(), "repeated manual request ignored while busy");
    worker.Cancel();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (worker.Busy() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto result = worker.TakeResult();
    check(!worker.Busy() && result && result->status == UpdateStatus::Cancelled && requests == 1, "disable cancels in-flight result");
    release = true; check(worker.Start(), "manual check remains usable after auto disabled");
    while (worker.Busy() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    result = worker.TakeResult();
    check(result && result->status == UpdateStatus::Available && requests == 2, "mock transport completion");
    check(!worker.TakeResult(), "one completion, no repeated notification");

    auto package = response("v1.2.6-holybear");
    const std::string installerJson = R"({"name":"NitLink-1.2.6-HolyBear-Setup-x64.exe","browser_download_url":"https://github.com/HolyBearTW/nitlink-holybear-edition/releases/download/v1.2.6-holybear/NitLink-1.2.6-HolyBear-Setup-x64.exe","digest":"sha256:f089e8d04ebe5b72423d1f2fea0d10894129f21ba8c78434d9518de4a58087d3","size":22})";
    package.body.replace(package.body.find("\"assets\":[]"), 11, "\"assets\":[" + installerJson + "]");
    package.body.replace(package.body.find("\"body\":null"), 11, R"("body":"# Changes\n\n**A fix**","published_at":"2026-11-03T00:00:00Z")");
    const auto parsedPackage = ClassifyReply(package);
    check(parsedPackage.installer && parsedPackage.installer->bytes == 22 &&
        parsedPackage.notes == L"# Changes\n\n**A fix**" && parsedPackage.publishedAt == L"2026-11-03T00:00:00Z",
        "release notes, date and canonical SHA-256 installer are parsed together");
    for (const auto& [field, replacement] : std::initializer_list<std::pair<std::string, std::string>>{
        {"sha256:f089", "sha256:X089"}, {"\"size\":22", "\"size\":0"}, {"\"size\":22", "\"size\":22.5"},
        {"/releases/download/", "/releases/tag/"}, {"/NitLink-1.2.6-HolyBear-Setup-x64.exe", "/other.exe"}}) {
        auto bad = package;
        bad.body.replace(bad.body.find(field), field.size(), replacement);
        check(ClassifyReply(bad).status == UpdateStatus::Invalid, "unverifiable or mismatched installer metadata is rejected");
    }
    check(IsUpdateDownloadRedirect(L"https://release-assets.githubusercontent.com/release-asset/file?signature=fixture"), "GitHub asset CDN redirect is accepted");
    for (const auto* url : {L"http://release-assets.githubusercontent.com/file", L"https://github.com.evil.test/file",
            L"https://user:password@github.com/file", L"https://release-assets.githubusercontent.com:8443/file",
            L"https://evil.test/file", L"file:///setup.exe", L"https://github.com/file#fragment"})
        check(!IsUpdateDownloadRedirect(url), "download redirects cannot escape the HTTPS GitHub hosts");
    if (parsedPackage.installer) {
        const auto directory = std::filesystem::temp_directory_path() /
            ("NitLink-Updater-Test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        const auto asset = *parsedPackage.installer;
        const std::string payload = "nitlink-update-fixture";
        auto wait = [&](UpdateInstaller& updater) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (updater.Busy() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check(!updater.Busy(), "download worker completes without a UI-thread wait");
            return updater.TakeState().value_or(TransferState{});
        };
        bool launchSucceeds = false;
        unsigned launches = 0;
        {
            UpdateInstaller updater(directory, [&](const InstallerAsset&, std::stop_token, const UpdateInstaller::Sink& sink) {
                return sink(std::string_view(payload).substr(0, 5)) && sink(std::string_view(payload).substr(5)) ? 0u : static_cast<uint32_t>(ERROR_WRITE_FAULT);
            }, [&](const std::filesystem::path& setup, const std::filesystem::path& target) {
                ++launches;
                check(setup == directory / asset.name && target == directory / L"installed app", "install launches only the verified native cache path with the selected application directory");
                return launchSucceeds;
            });
            check(!updater.Install(directory), "installer cannot launch before a verified download");
            check(updater.Start(asset), "native download starts");
            auto state = wait(updater);
            check(state.status == TransferStatus::Ready && state.received == 22 && state.tag == asset.tag &&
                std::filesystem::file_size(directory / asset.name) == 22 && !launches,
                "download writes, hashes and publishes Ready without automatically executing anything");
            check(!updater.Install(directory / L"installed app") && updater.TakeState()->status == TransferStatus::LaunchError,
                "a cancelled or failed installer launch leaves the app running and offers retry");
            launchSucceeds = true;
            check(updater.Install(directory / L"installed app") && !updater.Install(directory) && launches == 2,
                "explicit install retries successfully and cannot launch the same installer twice");
        }
        for (const std::string data : {"nitlink-update-fixturE", "short"}) {
            UpdateInstaller bad(directory, [&](const InstallerAsset&, std::stop_token, const UpdateInstaller::Sink& sink) {
                return sink(data) ? 0u : static_cast<uint32_t>(ERROR_WRITE_FAULT);
            });
            check(bad.Start(asset) && wait(bad).status == TransferStatus::Corrupt && !bad.Install(directory),
                "damaged or truncated bytes never become an installable package");
            check(!std::filesystem::exists(directory / (asset.name + L".partial")), "failed partial downloads are removed");
        }
        {
            UpdateInstaller cancelled(directory, [&](const InstallerAsset&, std::stop_token stop, const UpdateInstaller::Sink&) {
                while (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return static_cast<uint32_t>(ERROR_WINHTTP_OPERATION_CANCELLED);
            });
            check(cancelled.Start(asset) && !cancelled.Start(asset), "a second download request does not create a second worker");
            cancelled.Cancel();
            check(wait(cancelled).status == TransferStatus::Cancelled && !cancelled.Install(directory), "download cancellation prevents installation");
        }
        {
            UpdateInstaller offline(directory, [](const InstallerAsset&, std::stop_token, const UpdateInstaller::Sink&) {
                return static_cast<uint32_t>(ERROR_WINHTTP_TIMEOUT);
            });
            check(offline.Start(asset) && wait(offline).status == TransferStatus::NetworkError, "network timeouts are reported as retryable download errors");
        }
        std::error_code ignored;
        std::filesystem::remove(directory / asset.name, ignored);
        std::filesystem::remove(directory / (asset.name + L".partial"), ignored);
        std::filesystem::remove(directory, ignored);
    }
    std::cout << checks - failures << '/' << checks << " HolyBear update checks passed (mock responses; no network)\n";
    return failures ? 1 : 0;
}
