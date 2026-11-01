#pragma once
#include <functional>
#include <string>
#include <thread>

namespace NitLink::Accounts {
class PsnAuthBrowser {
public:
    enum class Outcome { Cookie, Cancelled, Timeout, Browser };
    struct Result { Outcome outcome; std::wstring npsso; };
    using Completion=std::function<void(Result)>;
    ~PsnAuthBrowser() { Stop(); }
    void Start(Completion complete);
    void Stop();
private:
    std::jthread m_thread;
};
}
