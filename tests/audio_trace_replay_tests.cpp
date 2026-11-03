#include "audio/audio_trace.h"
#include "audio/clock_recovery.h"
#include "audio/drift_controller.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace NitLink;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Simulation {
    double packetMs = 10, skew = 800, seconds = 60, sourceRate = 48000, renderRate = 48000;
    double deliveryJitter = .0003, timestampNoise = .000025;
    bool faults = false, deviceChange = false, stall = false, driftEnabled = true;
};
struct Result {
    AudioTraceSnapshot trace;
    unsigned earlyUnderruns = 0, steadyUnderruns = 0, trims = 0, retries = 0;
    unsigned fresh = 0, holding = 0, learning = 0;
    double maxFill = 0, estimate = 0, correction = 0;
};

// This is an actual feedback loop: consumed source frames depend on the last
// controller output. Device timestamps contain quantization/noise; the known
// oscillator skew is used only by the simulated hardware packet schedule.
// It is never passed to a controller or supplied as a clock estimate.
Result Simulate(const Simulation& options, bool retainTrace) {
    Result result;
    AudioClockRate capture, render; AudioClockBlend blend; DriftController drift;
    AudioRecoveryGuard recovery; CaptureProgressWatchdog watchdog;
    uint64_t sequence = 0, packetIndex = 0;
    double rate = options.sourceRate, epoch = 0, nextPacket = 0, lastPacket = 0;
    const double outputRate = options.renderRate, reserveSeconds = .003;
    const unsigned block = unsigned(std::lround(outputRate * .003));
    const double tick = block / outputRate;
    double fill = 0, phase = 31, lastControl = 0;
    bool primed = false, hasPlayed = false, changed = false;
    auto event = [&](AudioTraceEvent e) {
        e.sequence = ++sequence; e.session = 1; e.sourceRate = uint32_t(rate); e.renderRate = uint32_t(outputRate);
        e.fifoTargetFrames = uint32_t(rate * reserveSeconds);
        if (retainTrace) result.trace.events.push_back(e);
    };
    auto reset = [&](double now, uint32_t flags, DriftResetReason reason = DriftResetReason::Underflow) {
        if (flags & TraceNewSession) { capture.Reset(); render.Reset(); blend.Reset(); drift.Reset(); recovery.Reset(); watchdog.Reset(now); }
        if (flags & TraceResetClocks) { capture.Reset(); render.Reset(); blend.Reset(); }
        if (flags & TraceResetController) drift.Reset();
        if (flags & TraceClearRecovery) recovery.ClearWindow();
        if (flags & TraceResetWatchdog) watchdog.Reset(now);
        if (flags & TraceWatchdogRestarted) watchdog.EndpointRestarted(now);
        if (flags & TraceReprime) drift.Reprime(reason);
        AudioTraceEvent e; e.now = now; e.kind = AudioTraceKind::Reset; e.flags = flags; e.state = uint32_t(reason); event(e);
    };
    reset(0, TraceNewSession);
    auto schedule = [&] {
        const auto packet = std::lround(rate * options.packetMs / 1000);
        return epoch + (packetIndex + 1) * packet / (rate * (1 + options.skew / 1e6)) +
            .0013 + options.deliveryJitter * .5 * (1 + std::sin(packetIndex * 2.31));
    };
    nextPacket = schedule();
    for (unsigned step = 1; step * tick <= options.seconds; ++step) {
        const double now = step * tick;
        if (options.deviceChange && !changed && now >= 30) {
            changed = true; rate = 44100; epoch = now; packetIndex = 0; nextPacket = schedule();
            fill = 0; primed = false; phase = 31; lastControl = now;
            reset(now, TraceResetClocks | TraceResetController | TraceClearRecovery | TraceResetWatchdog, DriftResetReason::DeviceChange);
        }
        const double ratio = rate / outputRate;
        const unsigned taps = 64 * unsigned(std::ceil(std::max(1., ratio)));
        const unsigned history = taps / 2 - 1, lookahead = taps - history;
        if (!primed) phase = history;
        while (nextPacket <= now + 1e-10) {
            const unsigned packet = unsigned(std::lround(rate * options.packetMs / 1000));
            const double sampleTime = epoch + packetIndex * packet / (rate * (1 + options.skew / 1e6));
            const double delivered = nextPacket;
            const uint64_t position = packetIndex * packet;
            ++packetIndex; nextPacket = schedule();
            if (options.stall && delivered >= 22 && delivered < 23.2) continue;
            const bool timestampError = options.faults && delivered >= 15 && delivered < 15.4;
            double timestamp = 1 + sampleTime + options.timestampNoise * std::sin(packetIndex * 1.713);
            if (options.faults && packetIndex % 991 == 0) timestamp += .012;
            const uint64_t qpc = uint64_t(std::llround(timestamp * 1e7));
            watchdog.Progress(now, position, packet);
            if (timestampError) capture.RejectTimestamp(now); else capture.Sample(position, qpc, rate, now);
            AudioTraceEvent e; e.now = now; e.kind = AudioTraceKind::Capture; e.frames = packet;
            e.position = position; e.qpc = qpc; e.frequency = rate;
            e.packetInterval = lastPacket ? now - lastPacket : 0; lastPacket = now;
            e.flags = timestampError ? TraceTimestampError : 0; event(e);
            fill += packet;
        }
        const auto status = watchdog.Observe(now);
        AudioTraceEvent watch; watch.now = now; watch.kind = AudioTraceKind::Watchdog;
        watch.flags = (status.stalled ? TraceStalled : 0) | (status.restartCapture ? TraceRestartCapture : 0); event(watch);
        if (status.restartCapture) { ++result.retries; reset(now, TraceWatchdogRestarted); }

        const uint64_t renderPosition = uint64_t(std::llround((now - epoch) * outputRate));
        const uint64_t renderQpc = uint64_t(std::llround((1 + now + options.timestampNoise * std::sin(step * 2.123)) * 1e7));
        render.Sample(renderPosition, renderQpc, outputRate, now);
        AudioTraceEvent clock; clock.now = now; clock.kind = AudioTraceKind::RenderClock;
        clock.position = renderPosition; clock.qpc = renderQpc; clock.frequency = outputRate; event(clock);
        const auto estimate = blend.Update(capture, render, now);
        AudioTraceEvent fitted; fitted.now = now; fitted.kind = AudioTraceKind::ClockEstimate;
        fitted.clockPpm = estimate.ppm; fitted.confidence = estimate.confidence; fitted.state = uint32_t(estimate.state); event(fitted);
        result.estimate = estimate.ppm;
        result.fresh += estimate.state == AudioClockState::Fresh;
        result.holding += estimate.state == AudioClockState::Holding;
        result.learning += estimate.state == AudioClockState::Learning;

        const double reserve = std::max(rate * reserveSeconds, double(lookahead + 1));
        const double target = reserve + block * ratio;
        const double need = std::ceil(block * ratio * 1.002) + lookahead + 1;
        if (fill > history + target + rate * .020) {
            const double keep = history + reserve + need;
            if (fill > keep) {
                fill = keep; ++result.trims; phase = history; lastControl = now;
                reset(now, TraceReprime, DriftResetReason::BacklogTrim);
            }
        }
        if (!primed) {
            if (fill < history + reserve + need) {
                AudioTraceEvent silent; silent.now = now; silent.kind = AudioTraceKind::RenderResult;
                silent.want = block; silent.rebufferFrames = block;
                silent.flags = (hasPlayed ? 0 : TraceStartupSilence) | (options.driftEnabled ? TraceObserveRecovery : TraceClearRecovery);
                if (!options.driftEnabled) recovery.ClearWindow();
                else silent.recovery = uint32_t(recovery.Decide(now, 0, 0, tick));
                event(silent); continue;
            }
            primed = true; phase = history; lastControl = now; reset(now, TraceReprime);
        }
        const double error = (fill - history - target) / rate;
        const double headroom = (fill - phase - lookahead - block * ratio) / rate;
        const double dt = now - lastControl;
        const double ppm = options.driftEnabled ? drift.Update(error, dt, estimate.ppm, headroom) : 0;
        result.correction = ppm; lastControl = now;
        const double actualRatio = ratio * (1 + ppm / 1e6);
        AudioTraceEvent control; control.now = now; control.kind = AudioTraceKind::Control;
        control.flags = options.driftEnabled ? TraceDriftEnabled : 0; control.fill = uint32_t(fill);
        control.want = block; control.dt = dt; control.errorSeconds = error; control.headroomSeconds = headroom;
        control.clockPpm = estimate.ppm; control.ppm = ppm; control.ratio = actualRatio; event(control);
        unsigned written = 0;
        for (; written < block; ++written) {
            if (std::floor(phase) - history + taps > fill) break;
            phase += actualRatio;
        }
        const double consumed = std::floor(phase) - history; fill -= consumed; phase -= consumed;
        if (written < block) {
            if (now < 10 || (options.stall && now >= 22 && now < 26) ||
                (options.deviceChange && now >= 30 && now < 40)) ++result.earlyUnderruns;
            else ++result.steadyUnderruns;
            primed = false; phase = history; lastControl = now; reset(now, TraceReprime);
        }
        hasPlayed |= written > 0;
        const auto action = options.driftEnabled ? recovery.Decide(now, error, (block - written) / outputRate, tick) : AudioRecoveryAction::None;
        if (!options.driftEnabled) recovery.ClearWindow();
        AudioTraceEvent output; output.now = now; output.kind = AudioTraceKind::RenderResult;
        output.flags = options.driftEnabled ? TraceObserveRecovery : TraceClearRecovery;
        output.want = block; output.frames = written; output.missingFrames = block - written;
        output.errorSeconds = error; output.recovery = uint32_t(action); event(output);
        if (action != AudioRecoveryAction::None) {
            primed = false; phase = history; lastControl = now;
            reset(now, action == AudioRecoveryAction::Reprime ? TraceReprime :
                TraceResetClocks | TraceResetController | TraceClearRecovery);
        }
        if (now > 10) result.maxFill = std::max(result.maxFill, (fill - history) / rate);
    }
    return result;
}

void TestRing() {
    AudioTraceRecorder small(4, 3);
    AudioTraceEvent e;
    for (int i = 0; i < 3; ++i) Require(small.Push(e), "ring accepts capacity minus one");
    Require(!small.Push(e) && small.Dropped() == 1, "ring overflow is explicit and bounded");
    auto first = small.Snapshot(); Require(first.events.size() == 3, "snapshot drains queue");
    Require(small.Push(e), "consumer frees ring space");
    auto last = small.Snapshot();
    Require(last.historyDiscarded == 1 && last.events.back().sequence == 5, "history retention and gap sequence");
    AudioTraceRecorder threaded(256, 1024); threaded.Start();
    std::atomic<bool> finished{false};
    std::thread producer([&] {
        AudioTraceEvent item;
        for (unsigned i = 1; i <= 50000; ++i) { item.position = i; item.qpc = uint64_t(i) * 7; threaded.Push(item); }
        finished = true;
    });
    while (!finished) {
        const auto snapshot = threaded.Snapshot();
        for (const auto& item : snapshot.events) Require(item.qpc == item.position * 7, "concurrent event copy must not tear");
        std::this_thread::yield();
    }
    producer.join(); threaded.Stop();
    const auto snapshot = threaded.Snapshot();
    Require(snapshot.events.size() + snapshot.historyDiscarded + snapshot.dropped == 50000, "all concurrent events accounted for");
}

void TestExportFiles() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("nitlink-trace-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Require(std::filesystem::create_directory(directory), "reserve temporary test directory");
    const auto path = directory / std::filesystem::u8path("音频追踪.csv");
    AudioTraceRecorder recorder(8, 8); AudioTraceEvent event;
    event.packetInterval = 7200; event.dt = 7200; recorder.Push(event);
    std::string error;
    { std::ofstream old(path); old << "previous contents"; }
    Require(recorder.ExportCsv(path, error), "atomic export replaces Unicode target");
    AudioTraceSnapshot read;
    { std::ifstream input(path); Require(ReadAudioTraceCsv(input, read, error), "Unicode exported file parses"); }
    Require(read.events.size() == 1 && read.events[0].packetInterval == 7200, "long no-signal interval remains exportable");
    event.now = std::numeric_limits<double>::infinity(); recorder.Push(event);
    Require(!recorder.ExportCsv(path, error), "invalid export fails");
    { std::ifstream input(path); Require(ReadAudioTraceCsv(input, read, error), "failed export preserves old complete file"); }
    Require(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}) == 1,
        "temporary export directories are removed");
    std::filesystem::remove(path); std::filesystem::remove(directory);
}

void TestCsvAndReplay(const AudioTraceSnapshot& trace) {
    std::string error; std::ostringstream encoded;
    Require(WriteAudioTraceCsv(encoded, trace, error), "trace exports");
    const std::string csv = encoded.str();
    AudioTraceSnapshot decoded; std::istringstream input(csv);
    Require(ReadAudioTraceCsv(input, decoded, error), "real export parses");
    const auto report = ReplayAudioTrace(decoded);
    if (report.mismatches) PrintAudioReplayReport(std::cerr, report);
    Require(report.complete && report.checked > 1000 && report.mismatches == 0, "production decisions replay exactly");
    Require(report.startupSilenceFrames > 0 && report.rebufferFrames > 0, "startup and rebuffer silence are separate");
    auto tampered = decoded;
    for (auto& e : tampered.events) if (e.kind == AudioTraceKind::Control && e.now > 5) { e.ppm += 20; break; }
    Require(ReplayAudioTrace(tampered).mismatches == 1, "replay detects control tampering");
    auto truncated = decoded; truncated.events.erase(truncated.events.begin()); ++truncated.historyDiscarded;
    const auto partial = ReplayAudioTrace(truncated);
    Require(!partial.complete && partial.unverifiable > 0 && partial.checked == 0, "missing initial policy is never claimed verified");
    auto gap = decoded; gap.events.erase(gap.events.begin() + 200);
    Require(ReplayAudioTrace(gap).sequenceGaps == 1, "replay detects missing events");
    auto rejects = [&](std::string text) {
        std::istringstream bad(text); AudioTraceSnapshot target;
        Require(!ReadAudioTraceCsv(bad, target, error) && target.events.empty() && !error.empty(), "malformed input rejected transactionally");
    };
    std::string bad = csv; bad.replace(bad.find("nitlink_audio_trace,1"), 20, "nitlink_audio_trace,9"); rejects(bad);
    bad = csv; const auto first = bad.find('\n', bad.find('\n') + 1) + 1;
    bad.insert(first, 3000, 'x'); rejects(bad);
    bad = csv; const auto firstComma = bad.find(',', first); const auto secondComma = bad.find(',', firstComma + 1);
    bad.replace(firstComma + 1, secondComma - firstComma - 1, "nan"); rejects(bad);
    bad = csv; bad.insert(first, "-1,"); rejects(bad);
    bad = csv; const auto rowEnd = bad.find('\n', first); bad.insert(rowEnd + 1, bad.substr(first, rowEnd - first + 1)); rejects(bad);
    bad = csv; bad.replace(first, firstComma - first, "18446744073709551616"); rejects(bad);
    bad = csv; bad.insert(bad.find('\n'), std::string(1, '\0') + "hidden"); rejects(bad);
    auto invalid = trace; invalid.events[0].now = std::numeric_limits<double>::infinity();
    std::ostringstream rejected; Require(!WriteAudioTraceCsv(rejected, invalid, error), "export rejects nonfinite values");
}
}

int main(int argc, char** argv) {
    try {
        TestRing(); TestExportFiles();
        unsigned cases = 0;
        for (double packet : {1., 3., 10.}) for (double skew : {-1000., 0., 1000.}) {
            Simulation options; options.packetMs = packet; options.skew = skew;
            const auto result = Simulate(options, false); ++cases;
            if (result.steadyUnderruns || result.trims) {
                std::cerr << "packet=" << packet << " skew=" << skew << " underruns=" << result.steadyUnderruns << " trims=" << result.trims << '\n';
                throw std::runtime_error("no recurring underruns or trims in timestamp-driven closed loop");
            }
            Require(result.maxFill < .023 && std::abs(result.correction) <= 2000, "fixed 3 ms target remains bounded");
            Require(std::abs(result.estimate - skew) < 25, "noisy timestamps must recover actual device skew");
        }
        for (double source : {44100., 96000.}) {
            Simulation options; options.sourceRate = source; options.skew = -800;
            const auto result = Simulate(options, false); ++cases;
            Require(result.steadyUnderruns == 0 && result.trims == 0, "cross-rate clock-driven loop settles");
        }
        Simulation faults; faults.faults = true; faults.deviceChange = true; faults.stall = true; faults.seconds = 45;
        auto result = Simulate(faults, true); ++cases;
        Require(result.retries == 1, "capture stall issues one retry");
        Require(result.holding > 0 && result.fresh > 0 && result.learning > 0, "faults hold and device changes relearn clocks");
        Require(result.steadyUnderruns == 0 && result.trims == 0, "timestamp faults and reset settle without recurring damage");
        TestCsvAndReplay(result.trace);
        Simulation disabled; disabled.driftEnabled = false; disabled.skew = 0; disabled.seconds = 8;
        auto off = Simulate(disabled, true);
        Require(ReplayAudioTrace(off.trace).mismatches == 0, "disabled drift and recovery replay");
        if (argc == 3 && std::string(argv[1]) == "--export") {
            std::ofstream output(argv[2]); std::string error;
            Require(WriteAudioTraceCsv(output, result.trace, error), "simulation export");
        } else Require(argc == 1, "Usage: audio_trace_replay_tests [--export path.csv]");
        std::cout << "Audio trace: bounded concurrent capture, strict CSV validation and decision replay passed; "
                  << cases << " noisy-timestamp closed-loop scenarios, fixed 3 ms FIFO/output.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
