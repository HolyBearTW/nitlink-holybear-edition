// CPU tests for the frame differ's readback starvation rule (src/capture/readback_starvation.h).
#include "capture/readback_starvation.h"
#include <cstdio>
#include <cstdlib>

using NitLink::ReadbackStarvation;

static int checks = 0;
static void Require(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

// One frame of the differ's readback: busy = no copy finished without waiting; waitSucceeds = the bounded wait
// would get the oldest copy. Returns true when the frame got a classification.
static bool Frame(ReadbackStarvation& s, bool busy, bool waitAllowed, bool waitSucceeds = true) {
    if (!busy) { s.OnRead(false); return true; }
    if (s.OnBusy(waitAllowed) && waitSucceeds) { s.OnRead(true); return true; }
    return false;
}

int main() {
    {   // a GPU running two frames behind for 40 s with Source frame rate pacing: at most two blind frames
        ReadbackStarvation s;
        int blindRun = 0, longestBlind = 0, classified = 0;
        for (int i = 0; i < 2400; ++i) {
            if (Frame(s, true, true)) { ++classified; blindRun = 0; }
            else if (++blindRun > longestBlind) longestBlind = blindRun;
        }
        Require(longestBlind == ReadbackStarvation::kSkipsBeforeWait - 1, "starvation leaves at most two frames unclassified");
        Require(classified == 2400 - longestBlind, "after detection every busy frame waits and classifies");
    }
    {   // the same GPU without Source frame rate: the differ only feeds telemetry and never waits
        ReadbackStarvation s;
        int classified = 0;
        for (int i = 0; i < 600; ++i) classified += Frame(s, true, false);
        Require(classified == 0 && !s.starved, "no wait outside Source frame rate");
    }
    {   // occasional busy frames on a GPU that keeps up do not wait
        ReadbackStarvation s;
        bool waitedAny = false;
        for (int i = 0; i < 600; ++i) {
            const bool busy = i % 7 == 3 || i % 7 == 4;   // two busy frames in a row, then a finished copy
            if (busy) waitedAny |= s.OnBusy(true); else s.OnRead(false);
        }
        Require(!waitedAny && !s.starved, "one or two busy frames in a row never wait");
    }
    {   // recovery: once a copy finishes without waiting, the differ returns to non-blocking reads
        ReadbackStarvation s;
        for (int i = 0; i < 10; ++i) Frame(s, true, true);
        Require(s.starved, "starved after sustained busy frames");
        Frame(s, false, true);
        Require(!s.starved && s.consecutiveBusy == 0, "a finished copy without waiting ends the starvation");
        Require(!s.OnBusy(true), "the next single busy frame skips again instead of waiting");
    }
    {   // waits that run out of time keep the starvation state, so the next frame waits again
        ReadbackStarvation s;
        int classified = 0;
        for (int i = 0; i < 20; ++i) classified += Frame(s, true, true, false);
        Require(classified == 0 && s.starved, "timed-out waits stay starved");
        Require(Frame(s, true, true, true), "the next wait that succeeds classifies");
    }
    {   // switching away from Source frame rate mid-starvation stops waiting at once
        ReadbackStarvation s;
        for (int i = 0; i < 10; ++i) Frame(s, true, true);
        Require(!s.OnBusy(false), "no wait once Source frame rate is off");
        s.Reset();
        Require(!s.starved && s.consecutiveBusy == 0, "reset clears the state");
    }
    std::printf("{\"status\":\"READBACK_STARVATION_PASS\",\"checks\":%d}\n", checks);
    return 0;
}
