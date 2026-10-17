#include "capture/frame_readback_queue.h"
#include "capture/noise_floor.h"
#include "capture/readback_starvation.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void Require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}

void CheckCadence(unsigned cadence) {
    NitLink::FrameReadbackQueue queue;
    std::array<unsigned, 10> newCounts{};
    uint64_t nextExpected = 1;
    unsigned unavailable = 0;
    for (uint64_t frame = 1; frame <= 600; ++frame) {
        const uint64_t latency = frame >= 120 && frame < 420 && frame % 5 == 0 ? 2 : 1;
        const uint64_t completedThrough = frame - latency;
        if (!queue.Empty() && queue.ReadFrame() <= completedThrough) {
            const uint64_t compared = queue.ReadFrame();
            Require(compared == nextExpected++, "completion timing cannot repeat or skip a comparison");
            Require(!queue.FrontHasGap(), "one/two-frame completion changes retain consecutive inputs");
            if (cadence != 0 && (cadence == 1 || compared % cadence == 0))
                ++newCounts[static_cast<size_t>((frame - 1) / 60)];
            Require(queue.Consume(), "completed result is consumed once");
        } else if (frame > 1) {
            ++unavailable;
        }
        Require(queue.Submit(frame), "two-frame completion never overwrites an unread copy");
    }
    Require(unavailable == 1, "only the initial increase in completion latency lacks a result");
    for (size_t second = 1; second < newCounts.size(); ++second) {
        const unsigned expected = cadence == 0 ? 0 : 60 / cadence;
        Require(newCounts[second] <= expected, "completion jitter cannot inflate the new-content count");
        Require(newCounts[second] + 1 >= expected, "completion jitter loses at most its single unavailable sample");
    }
    while (!queue.Empty()) {
        Require(queue.ReadFrame() == nextExpected++, "remaining copies drain in source order");
        queue.Consume();
    }
    Require(nextExpected == 601, "all submitted comparisons are consumed exactly once");
}

void CheckCapacityAndGaps() {
    NitLink::FrameReadbackQueue queue;
    Require(queue.Submit(2) && queue.Submit(3) && queue.Submit(4), "all staging slots can hold unread copies");
    const uint32_t oldestSlot = queue.ReadSlot();
    Require(queue.Full() && !queue.Submit(5) && !queue.Submit(6), "full staging queues reject new copies");
    Require(queue.ReadSlot() == oldestSlot && queue.ReadFrame() == 2, "full queues preserve their oldest unread result");
    for (uint64_t frame = 2; frame <= 4; ++frame) {
        Require(queue.ReadFrame() == frame && !queue.FrontHasGap(), "capacity pressure preserves queued comparison identities");
        queue.Consume();
    }
    Require(queue.Submit(7) && queue.FrontHasGap(), "the next accepted comparison exposes dropped inputs");
    queue.Consume();
    Require(queue.Submit(8) && !queue.FrontHasGap(), "consecutive input resumes after the reported gap");
    queue.Discard();
    Require(queue.Submit(9) && queue.FrontHasGap(), "a failed read exposes a gap without consuming its result");
    queue.Consume();
    Require(!queue.Submit(9), "an old comparison cannot be resubmitted");
    queue.Reset();
    Require(queue.Empty() && queue.LastConsumedFrame() == 0 && queue.WriteSlot() == 0,
            "reset clears pending identities and slot ownership");
    Require(queue.Submit(2) && !queue.FrontHasGap(), "a new stream starts without old gaps or results");
}

void CheckStarvation() {
    NitLink::FrameReadbackQueue queue;
    NitLink::ReadbackStarvation starvation;
    queue.Submit(1);
    for (unsigned busy = 1; busy <= 8; ++busy) {
        const bool wait = starvation.OnBusy(true);
        Require(wait == (busy >= NitLink::ReadbackStarvation::kSkipsBeforeWait),
                "ordered reads retain the bounded-wait activation threshold");
        Require(queue.ReadFrame() == 1, "busy and timed-out reads leave the same comparison pending");
    }
    starvation.OnRead(true);
    Require(queue.Consume() && starvation.starved, "a successful bounded wait consumes once and retains recovery state");
    Require(queue.Submit(2), "the freed staging slot is reusable");
    starvation.OnRead(false);
    queue.Consume();
    Require(!starvation.starved && starvation.consecutiveBusy == 0,
            "a nonblocking completion restores ordinary reads");
    for (unsigned i = 0; i < 20; ++i)
        Require(!starvation.OnBusy(false), "telemetry-only reads never wait");
}

void CheckNoiseHistoryGap() {
    NitLink::NoiseFloorEstimator floor;
    for (unsigned i = 0; i < 48; ++i) floor.Observe(i % 2 ? 0.0012f : 0.02f);
    Require(floor.Valid(), "consecutive repeat/change samples establish a floor");
    const float learned = floor.Floor();
    const uint32_t updates = floor.Updates();
    floor.ResetHistory();
    for (unsigned i = 0; i < 15; ++i) floor.Observe(i % 2 ? 0.0005f : 0.02f);
    Require(floor.Valid() && floor.Floor() == learned && floor.Updates() == updates,
            "a gap preserves learned noise but cannot combine disconnected windows");
    for (unsigned i = 15; i < 23; ++i) floor.Observe(i % 2 ? 0.0005f : 0.02f);
    Require(floor.Updates() > updates && floor.Floor() < learned,
            "a complete consecutive window can update the retained floor");
    floor.Reset();
    Require(!floor.Valid() && floor.Floor() == 0.0f, "stream reset still forgets the learned floor");
}
}

int main() {
    try {
        CheckCadence(2);
        CheckCadence(1);
        CheckCadence(0);
        CheckCapacityAndGaps();
        CheckStarvation();
        CheckNoiseHistoryGap();
        std::cout << "PASS ordered readback identities, completion jitter, 30/60/static cadence, capacity, gaps, reset and starvation; checks=" << checks << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
