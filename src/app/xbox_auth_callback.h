#pragma once
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace NitLink::Accounts {
// Owns the temporary loopback listener only. Token exchange stays in AccountService.
class XboxAuthCallback {
public:
    enum class Outcome { Code, Denied, Rejected, Timeout, Network };
    struct Result { Outcome outcome; std::wstring code; };
    using Completion = std::function<void(Result)>;
    XboxAuthCallback() = default;
    ~XboxAuthCallback();
    std::optional<std::wstring> Start(std::wstring state, Completion complete,
        std::chrono::milliseconds timeout = std::chrono::minutes(5));
    void Stop();
private:
    bool m_winsock = false;
    std::jthread m_thread;
};
}
