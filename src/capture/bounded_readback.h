#pragma once
#include <chrono>
#include <dxgi.h>

namespace NitLink {
struct ReadbackAttempt {
    HRESULT status;
    bool waited;
    bool timedOut;
};

// An unavailable GPU result stays unknown when the polling budget expires.
// A zero budget leaves telemetry nonblocking and does not flush commands.
template <class Map, class Flush, class Now, class Yield>
ReadbackAttempt PollReadback(Map map, Flush flush, Now now, Yield yield,
                            std::chrono::milliseconds budget) {
    const auto deadline = now() + budget;
    HRESULT status = map();
    if (status != DXGI_ERROR_WAS_STILL_DRAWING || budget.count() <= 0)
        return {status, false, false};
    flush();
    while (status == DXGI_ERROR_WAS_STILL_DRAWING && now() < deadline) {
        yield();
        if (now() >= deadline) break;
        status = map();
    }
    return {status, true, status == DXGI_ERROR_WAS_STILL_DRAWING};
}
} // namespace NitLink