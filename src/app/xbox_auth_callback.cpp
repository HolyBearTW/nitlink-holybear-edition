#include "xbox_auth_callback.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <utility>

namespace NitLink::Accounts {
namespace {
struct Socket {
    SOCKET value = INVALID_SOCKET;
    explicit Socket(SOCKET socket) : value(socket) {}
    Socket(Socket&& other) noexcept : value(std::exchange(other.value, INVALID_SOCKET)) {}
    Socket(const Socket&) = delete;
    ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
};
}

XboxAuthCallback::~XboxAuthCallback() {
    Stop();
    if (m_winsock) WSACleanup();
}

void XboxAuthCallback::Stop() {
    if (m_thread.joinable()) { m_thread.request_stop(); m_thread.join(); }
}

std::optional<std::wstring> XboxAuthCallback::Start(std::wstring state, Completion complete,
    std::chrono::milliseconds timeout) {
    Stop();
    if (!m_winsock) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data)) return std::nullopt;
        m_winsock = true;
    }
    Socket server(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (server.value == INVALID_SOCKET) return std::nullopt;
    BOOL exclusive = TRUE;
    if (setsockopt(server.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive))) return std::nullopt;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0; // Let Windows choose an available port, including when another login port is occupied.
    if (bind(server.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
        listen(server.value, SOMAXCONN)) return std::nullopt;
    u_long nonblocking = 1;
    if (ioctlsocket(server.value, FIONBIO, &nonblocking)) return std::nullopt;
    int length = sizeof(address);
    if (getsockname(server.value, reinterpret_cast<sockaddr*>(&address), &length)) return std::nullopt;
    const auto port = std::to_string(ntohs(address.sin_port));
    const auto redirect = L"http://localhost:" + std::to_wstring(ntohs(address.sin_port)) + L"/";
    m_thread = std::jthread([server = std::move(server), state = std::move(state), port,
        complete = std::move(complete), timeout](std::stop_token stop) mutable {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::optional<Result> result;
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            WSAPOLLFD incoming{server.value, POLLRDNORM, 0};
            const auto available = WSAPoll(&incoming, 1, 100);
            if (available == SOCKET_ERROR) { result = Result{Outcome::Network, {}}; break; }
            if (!available) continue;
            if (!(incoming.revents & POLLRDNORM)) { result = Result{Outcome::Network, {}}; break; }
            Socket client(accept(server.value, nullptr, nullptr));
            if (client.value == INVALID_SOCKET) continue;
            u_long nonblocking = 1;
            if (ioctlsocket(client.value, FIONBIO, &nonblocking)) continue;
            std::string request;
            std::array<char, 2048> buffer{};
            const auto requestDeadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(2));
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < requestDeadline &&
                request.size() < 16384 && request.find("\r\n\r\n") == std::string::npos) {
                WSAPOLLFD input{client.value, POLLRDNORM, 0};
                if (WSAPoll(&input, 1, 100) <= 0) continue;
                const auto count = recv(client.value, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (count <= 0) break;
                request.append(buffer.data(), static_cast<size_t>(count));
            }
            if (stop.stop_requested()) break;
            int status = 400;
            const auto lineEnd = request.find("\r\n");
            const auto targetEnd = request.find(' ', 4);
            if (request.starts_with("GET ") && targetEnd != request.npos && targetEnd < lineEnd &&
                request.find("\r\n\r\n") != request.npos && request.size() <= 16384) {
                const auto target = request.substr(4, targetEnd - 4);
                const auto query = target.find('?');
                bool hostMatches = false;
                size_t header = lineEnd + 2;
                while (header < request.size()) {
                    const auto end = request.find("\r\n", header);
                    if (end == request.npos || end == header) break;
                    auto line = request.substr(header, end - header);
                    const auto colon = line.find(':');
                    if (colon != line.npos) {
                        auto name = line.substr(0, colon);
                        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (name == "host") {
                            auto host = line.substr(colon + 1);
                            const auto first = host.find_first_not_of(" \t"), last = host.find_last_not_of(" \t");
                            host = first == host.npos ? "" : host.substr(first, last - first + 1);
                            hostMatches = host == "localhost:" + port || host == "127.0.0.1:" + port;
                        }
                    }
                    header = end + 2;
                }
                if (target.substr(0, query) != "/") status = 404;
                else if (hostMatches && query != target.npos) {
                    // Validate the browser response once at the HTTP boundary. Never echo or log its query.
                    try {
                        winrt::Windows::Foundation::WwwFormUrlDecoder fields(winrt::to_hstring(target.substr(query + 1)));
                        std::wstring returnedState, code, error;
                        unsigned states = 0, codes = 0, errors = 0;
                        for (const auto& field : fields) {
                            if (field.Name() == L"state") { ++states; returnedState = field.Value(); }
                            if (field.Name() == L"code") { ++codes; code = field.Value(); }
                            if (field.Name() == L"error") { ++errors; error = field.Value(); }
                        }
                        if (states != 1 || returnedState != state) status = 403;
                        else if (codes == 1 && errors == 0 && !code.empty() && code.size() <= 4096 &&
                            std::none_of(code.begin(),code.end(),[](wchar_t c){return c<0x20||c==0x7f;})) {
                            result = Result{Outcome::Code, std::move(code)}; status = 200;
                        } else if (errors == 1 && codes == 0) {
                            result = Result{error == L"access_denied" ? Outcome::Denied : Outcome::Rejected, {}}; status = 200;
                        }
                    } catch (const winrt::hresult_error&) { status = 400; }
                }
            }
            const std::string message = status == 200 ?
                "Sign-in response received. You can close this tab and return to NitLink to check the result." :
                "This sign-in response could not be accepted. Return to NitLink to continue.";
            const auto body = "<!doctype html><meta charset=utf-8><title>NitLink</title><p>" + message + "</p>";
            const auto response = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK\r\n" : " Rejected\r\n") +
                "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n"
                "Content-Security-Policy: default-src 'none'; frame-ancestors 'none'; base-uri 'none'\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\n\r\n" + body;
            size_t sent = 0;
            const auto sendDeadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(1));
            while (sent < response.size() && !stop.stop_requested() && std::chrono::steady_clock::now() < sendDeadline) {
                WSAPOLLFD output{client.value, POLLWRNORM, 0};
                if (WSAPoll(&output, 1, 100) <= 0) continue;
                const auto count = send(client.value, response.data() + sent, static_cast<int>(response.size() - sent), 0);
                if (count <= 0) break;
                sent += static_cast<size_t>(count);
            }
            if (result) break; // One response consumes this listener; duplicate callbacks cannot trigger another exchange.
        }
        if (!stop.stop_requested()) complete(result.value_or(Result{Outcome::Timeout, {}}));
        winrt::clear_factory_cache();
        winrt::uninit_apartment();
    });
    return redirect;
}
}
