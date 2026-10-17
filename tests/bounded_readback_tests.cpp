#include "capture/bounded_readback.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void Require(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
void Scenario(unsigned readyAt, bool fail, int budget, unsigned flushCost = 0) {
    using namespace std::chrono;
    unsigned calls = 0, flushes = 0, ticks = 0;
    const auto result = NitLink::PollReadback(
        [&]() -> HRESULT { ++calls; return calls >= readyAt ? (fail ? E_FAIL : S_OK) : DXGI_ERROR_WAS_STILL_DRAWING; },
        [&] { ++flushes; ticks += flushCost; },
        [&] { return steady_clock::time_point{} + milliseconds(ticks); },
        [&] { ++ticks; }, milliseconds(budget));
    Require(flushes <= 1, "a busy comparison submits commands at most once");
    if (readyAt == 1) {
        Require(calls == 1 && !flushes && !result.waited && !result.timedOut, "an immediate result needs no polling or flush");
        Require(result.status == (fail ? E_FAIL : S_OK), "immediate failures remain failures");
    } else if (budget == 0) {
        Require(calls == 1 && !flushes && !ticks && !result.waited && !result.timedOut,
            "asynchronous telemetry neither waits nor flushes");
    } else if (readyAt <= 8 && !flushCost) {
        Require(result.status == (fail ? E_FAIL : S_OK) && result.waited && !result.timedOut,
            "the current result or actual failure is returned within budget");
        Require(calls == readyAt && ticks == readyAt - 1, "polling stops on the first completed result");
    } else {
        Require(result.status == DXGI_ERROR_WAS_STILL_DRAWING && result.waited && result.timedOut,
            "a late result remains unknown at the deadline");
        Require(ticks == (flushCost ? flushCost : 8), "polling adds no wait beyond the deadline");
        Require(calls <= 8, "the deadline bounds polling attempts even under permanent starvation");
    }
}
}
int main() {
    try {
        Scenario(1, false, 8); Scenario(1, true, 8);
        Scenario(2, false, 0); Scenario(999, false, 0);
        for (unsigned i = 2; i <= 8; ++i) { Scenario(i, false, 8); Scenario(i, true, 8); }
        Scenario(999, false, 8); Scenario(999, false, 8, 10);
        std::cout << "PASS bounded current-readback completion, timeout, failure and nonblocking telemetry; checks=" << checks << '\n';
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}