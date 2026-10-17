#pragma once
// When the frame differ's non-blocking readback may wait for the GPU.
//
// FrameDiffer reads each comparison result back from a ring of staging copies without waiting. When both copies
// stay in flight for several frames in a row, no classification arrives and "Source frame rate" pacing presents
// every captured frame, repeats included (seen for 43 s with VSync on, a 60 Hz display and a frame generator
// running on the same GPU).
//
// The rule: after kSkipsBeforeWait busy readbacks in a row, and only while the caller allows waiting (the
// classification drives presentation), the differ waits a bounded time for the oldest copy. Once starvation is
// detected it waits on every busy readback until a non-blocking read succeeds again: a starved frame gets a
// classification whenever its oldest copy finishes within the wait, and the loop returns to non-blocking reads as
// soon as the GPU keeps up.
#include <cstdint>

namespace NitLink {

struct ReadbackStarvation {
    static constexpr uint32_t kSkipsBeforeWait = 3;

    uint32_t consecutiveBusy = 0;
    bool starved = false;

    // Both copies were still in flight. Returns true when the caller should wait (bounded) for the oldest copy.
    bool OnBusy(bool waitAllowed) {
        ++consecutiveBusy;
        if (waitAllowed && consecutiveBusy >= kSkipsBeforeWait) starved = true;
        return waitAllowed && starved;
    }

    // A result was read; waited is true when the read needed the bounded wait.
    void OnRead(bool waited) {
        consecutiveBusy = 0;
        if (!waited) starved = false;
    }

    void Reset() {
        consecutiveBusy = 0;
        starved = false;
    }
};

}
