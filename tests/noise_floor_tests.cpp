// CPU tests for the adaptive duplicate floor (src/capture/noise_floor.h), plus an optional replay of a recorded
// per-frame trace (a CSV with a "difference" column holding the differ's frame-global mean) that prints the learned
// floor and the resulting unique-frame rate through the differ's stage-1 hysteresis.
#include "noise_floor.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using NitLink::NoiseFloorEstimator;

static int checks = 0;
static void Require(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

// Stage-1 hysteresis of FrameDiffer::Process with the estimator's bounds; returns the committed "new" votes.
// stickyDeadband: the previous verdict always stands between the bounds (no alternation rule), for comparison.
static std::vector<bool> Classify(const std::vector<float>& diffs, NoiseFloorEstimator& est, float baseUpper = 0.0005f,
                                  float baseLower = 0.0002f, bool stickyDeadband = false) {
    std::vector<bool> votes;
    bool wasNew = true, lastVote = true, voteBefore = true;
    for (float d : diffs) {
        est.Observe(d);
        const auto b = est.Compute(baseUpper, baseLower);
        const bool alternating = !stickyDeadband && votes.size() >= 2 && lastVote != voteBefore;
        const bool raw = NitLink::ClassifyAgainstBounds(d, b.lower, b.upper, wasNew, alternating);
        wasNew = raw;
        voteBefore = lastVote;
        lastVote = raw;
        votes.push_back(raw);
    }
    return votes;
}

// Capture of a noisy card shaped after a measured UGREEN 1080p60 trace: repeat noise level changing
// with the picture every 20-60 frames between levelLo and levelHi, +-15% per frame; new frames add motion, log-uniform
// in [motionLo, motionHi], or in [0.0006, 0.0015] during dark slow stretches. kind 0 = 30 fps (change, repeat),
// 1 = 60 fps (every frame a change).
struct Sample { float diff; bool isNew; };
static std::vector<Sample> NoisyCapture(uint64_t seed, int frames, int kind, double darkShare, double levelLo,
                                        double levelHi, double motionLo = 0.002, double motionHi = 0.04) {
    uint64_t s = seed * 2654435761u + 1;
    auto u = [&s]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (s >> 11) * (1.0 / 9007199254740992.0); };
    auto logRange = [&u](double a, double b) { return a * std::pow(b / a, u()); };
    std::vector<Sample> out;
    int segLeft = 0; double level = levelLo; bool dark = false;
    for (int i = 0; i < frames; ++i) {
        if (segLeft-- <= 0) { segLeft = 20 + (int)(40 * u()); level = levelLo + (levelHi - levelLo) * u(); dark = u() < darkShare; }
        const bool isNew = kind == 1 || i % 2 == 0;
        double d = level * (0.85 + 0.35 * u());
        if (isNew) d += dark ? logRange(0.0006, 0.0015) : logRange(motionLo, motionHi);
        out.push_back({(float)d, isNew});
    }
    return out;
}

struct Errors { double repeatsAsNew = 0, missed = 0; };
static Errors Measure(const std::vector<Sample>& capture, bool stickyDeadband) {
    std::vector<float> diffs;
    for (const auto& c : capture) diffs.push_back(c.diff);
    NoiseFloorEstimator e;
    const auto votes = Classify(diffs, e, 0.0005f, 0.0002f, stickyDeadband);
    long repeats = 0, news = 0, asNew = 0, missed = 0;
    for (size_t i = 120; i < capture.size(); ++i) {
        if (capture[i].isNew) { ++news; missed += !votes[i]; } else { ++repeats; asNew += votes[i]; }
    }
    return {repeats ? 100.0 * asNew / repeats : 0.0, news ? 100.0 * missed / news : 0.0};
}

static std::vector<float> Alternating(float low, float high, int n, float jitter = 0.0f) {
    std::vector<float> v;
    for (int i = 0; i < n; ++i) {
        const float j = jitter * ((i * 7919) % 13 - 6) / 6.0f;
        v.push_back(i % 2 ? low * (1.0f + j) : high * (1.0f + j));
    }
    return v;
}

int main(int argc, char** argv) {
    {   // 4K Pro at 1440p: repeats 0.00066, slow pan 0.0034 -> floor learned, repeats rejected, motion kept
        NoiseFloorEstimator e;
        const auto votes = Classify(Alternating(0.00066f, 0.0034f, 120, 0.03f), e);
        Require(e.Valid() && e.Floor() > 0.0005f && e.Floor() < 0.0009f, "1440p floor learned near 0.00066");
        int news = 0; for (size_t i = 60; i < votes.size(); ++i) news += votes[i];
        Require(news == 30, "after learning, exactly half of the frames are new (30 of 60)");
        const auto b = e.Compute(0.0005f, 0.0002f);
        Require(b.adaptive && b.lower > 0.0008f && b.upper >= 0.0012f && b.upper < 0.0019f, "bounds sit above the noise and under the smallest measured motion");
    }
    {   // the same card during a slow pan at 0.00187 (ratio 2.8): floor still learned (separation 2.5), repeats rejected
        NoiseFloorEstimator e;
        const auto votes = Classify(Alternating(0.00066f, 0.00187f, 120, 0.02f), e);
        Require(e.Valid(), "slow-pan window still learns the floor");
        int news = 0; for (size_t i = 60; i < votes.size(); ++i) news += votes[i];
        Require(news == 30, "slow pan: half new");
    }
    {   // UGREEN-like: noisier repeats 0.0012, 30 fps game 0.03
        NoiseFloorEstimator e;
        const auto votes = Classify(Alternating(0.0012f, 0.03f, 120, 0.05f), e);
        Require(e.Valid() && e.Floor() > 0.001f && e.Floor() < 0.0014f, "noisier card floor learned");
        int news = 0; for (size_t i = 60; i < votes.size(); ++i) news += votes[i];
        Require(news == 30, "noisier card: half new");
    }
    {   // exact repeats (1080p/4K): low band 0 -> no floor, the base thresholds apply unchanged
        NoiseFloorEstimator e;
        const auto votes = Classify(Alternating(0.0f, 0.02f, 120), e);
        Require(!e.Valid(), "exact repeats learn no floor");
        int news = 0; for (size_t i = 60; i < votes.size(); ++i) news += votes[i];
        Require(news == 30, "exact repeats: half new with base thresholds");
    }
    {   // real 60 fps content with mixed motion: no alternation -> no floor, every frame new
        NoiseFloorEstimator e;
        std::vector<float> v;
        for (int i = 0; i < 120; ++i) v.push_back(0.002f + 0.03f * ((i * 7919) % 17) / 17.0f);
        const auto votes = Classify(v, e);
        Require(!e.Valid(), "60 fps mixed motion learns no floor");
        int news = 0; for (bool b : votes) news += b;
        Require(news == 120, "60 fps content stays all new");
    }
    {   // static picture with noise 0.0006 everywhere: no separation -> no floor (the base rule decides)
        NoiseFloorEstimator e;
        std::vector<float> v(120, 0.0006f);
        Classify(v, e);
        Require(!e.Valid(), "static noise learns no floor");
    }
    {   // a learned floor survives a 60 fps or static stretch and a slow pan; Reset forgets it
        NoiseFloorEstimator e;
        Classify(Alternating(0.00066f, 0.0034f, 64, 0.02f), e);
        Require(e.Valid(), "learned");
        std::vector<float> v(64, 0.02f);
        Classify(v, e);
        Require(e.Valid() && e.Floor() > 0.0005f, "floor kept through 60 fps content");
        e.Reset();
        Require(!e.Valid() && e.Floor() == 0.0f, "reset forgets the floor");
    }
    {   // a repeat band above kMaxFloor is motion, never a floor (a 60 fps game alternating 0.003/0.03 must not lose frames)
        NoiseFloorEstimator e;
        const auto votes = Classify(Alternating(0.003f, 0.03f, 120), e);
        Require(!e.Valid(), "bands above the noise ceiling are not a floor");
        int news = 0; for (bool b : votes) news += b;
        Require(news == 120, "alternating real motion keeps every frame");
    }
    {   // noise drifting with the picture (0.0004-0.0011) and dark slow stretches: the sticky deadband reads repeats as
        // changes and misses small changes; following the alternation in the deadband cuts both
        Errors now, sticky;
        for (uint64_t seed = 1; seed <= 3; ++seed) {
            const auto capture = NoisyCapture(seed, 60 * 120, 0, 0.30, 0.0004, 0.0011);
            const auto a = Measure(capture, false), b = Measure(capture, true);
            now.repeatsAsNew += a.repeatsAsNew / 3; now.missed += a.missed / 3;
            sticky.repeatsAsNew += b.repeatsAsNew / 3; sticky.missed += b.missed / 3;
        }
        std::printf("drifting noise, 30%% dark: repeats as new %.2f%% (sticky %.2f%%), missed %.2f%% (sticky %.2f%%)\n",
                    now.repeatsAsNew, sticky.repeatsAsNew, now.missed, sticky.missed);
        Require(now.repeatsAsNew < 7.0 && now.repeatsAsNew < 0.5 * sticky.repeatsAsNew, "drifting noise: far fewer repeats read as new");
        Require(now.missed < 4.0 && now.missed < 0.5 * sticky.missed, "drifting noise: far fewer real frames missed");
    }
    {   // steady noise with dark slow stretches: small real changes after a repeat count as changes
        Errors now{}, sticky{};
        for (uint64_t seed = 1; seed <= 3; ++seed) {
            const auto capture = NoisyCapture(seed, 60 * 120, 0, 0.30, 0.0007, 0.0007);
            const auto a = Measure(capture, false), b = Measure(capture, true);
            now.missed += a.missed / 3; sticky.missed += b.missed / 3; now.repeatsAsNew += a.repeatsAsNew / 3;
        }
        Require(now.missed < 0.5 && now.missed < sticky.missed, "steady noise, dark scenes: almost no real frame missed");
        Require(now.repeatsAsNew < 0.5, "steady noise: repeats stay repeats");
    }
    {   // 60 fps content on the same noisy card, including slow motion near the noise: no more frames lost than with the sticky deadband
        for (int slow = 0; slow < 2; ++slow) {
            Errors now{}, sticky{};
            for (uint64_t seed = 1; seed <= 3; ++seed) {
                auto capture = NoisyCapture(seed + 50, 1200, 0, 0.0, 0.0004, 0.0011);   // floor learned on 30 fps first
                const auto body = NoisyCapture(seed, 60 * 120, 1, 0.0, 0.0004, 0.0011, slow ? 0.0008 : 0.002, slow ? 0.003 : 0.04);
                capture.insert(capture.end(), body.begin(), body.end());
                const auto a = Measure(capture, false), b = Measure(capture, true);
                now.missed += a.missed / 3; sticky.missed += b.missed / 3;
            }
            Require(now.missed <= sticky.missed + 0.2, slow ? "slow 60 fps: frames kept as well as with the sticky deadband" : "60 fps: frames kept as well as with the sticky deadband");
        }
    }
    if (argc > 1) {   // replay a recorded trace: print floor and unique rate per second
        std::ifstream in(argv[1]);
        std::string line; std::getline(in, line);
        int col = -1; { std::stringstream h(line); std::string c; int i = 0; while (std::getline(h, c, ',')) { if (c == "difference") col = i; ++i; } }
        Require(col >= 0, "difference column present");
        std::vector<float> diffs;
        while (std::getline(in, line)) {
            std::stringstream s(line); std::string c; int i = 0;
            while (std::getline(s, c, ',')) { if (i == col) diffs.push_back(std::strtof(c.c_str(), nullptr)); ++i; }
        }
        NoiseFloorEstimator e;
        const auto votes = Classify(diffs, e);
        std::printf("replay %s: frames %zu floor %.5f valid %d updates %u\n", argv[1], diffs.size(), e.Floor(), e.Valid(), e.Updates());
        for (size_t start = 0; start + 60 <= votes.size(); start += 600) {
            int news = 0; for (size_t i = start; i < start + 60; ++i) news += votes[i];
            std::printf("  frames %5zu-%5zu: %d new of 60\n", start, start + 60, news);
        }
    }
    std::printf("{\"status\":\"NOISE_FLOOR_PASS\",\"checks\":%d}\n", checks);
    return 0;
}
