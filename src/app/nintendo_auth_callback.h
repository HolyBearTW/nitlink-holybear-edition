#pragma once
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace NitLink::Accounts {
class NintendoAuthCallback {
public:
    enum class StartResult { Ready, InUse, Failed };
    enum class Outcome { Code, Denied, Rejected, Timeout };
    struct Result { Outcome outcome; std::wstring code; };
    using Completion=std::function<void(Result)>;
    ~NintendoAuthCallback() { Stop(); }
    StartResult Start(std::wstring state, Completion complete);
    void Stop();
private:
    std::jthread m_thread;
};

// The browser launches a short-lived helper invocation; capture/MF never starts.
std::optional<int> ForwardNintendoAuthCallback();
}
