#include "audio_trace.h"
#include "clock_recovery.h"
#include "drift_controller.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace NitLink {
static_assert(std::is_trivially_copyable<AudioTraceEvent>::value, "Trace events must remain POD copies");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "Trace counters must be lock-free");
static_assert(std::atomic<size_t>::is_always_lock_free, "Trace indexes must be lock-free");

struct AudioTraceRecorder::Impl {
    Impl(size_t queueSize, size_t historySize) : queue(queueSize), history(historySize) {
        if (queueSize < 2 || queueSize > AudioTraceMaxEvents || !historySize || historySize > AudioTraceMaxEvents)
            throw std::invalid_argument("Invalid audio trace capacity");
    }
    std::vector<AudioTraceEvent> queue, history;
    alignas(64) std::atomic<size_t> write{0};
    alignas(64) std::atomic<size_t> read{0};
    std::atomic<uint64_t> dropped{0};
    uint64_t sequence = 0, discarded = 0;
    size_t historyStart = 0, historyCount = 0;
    std::mutex mutex, waitMutex;
    std::condition_variable wake;
    std::atomic<bool> running{false};
    std::thread consumer;

    void Drain() {
        // Called only by the serialized non-real-time consumer/Snapshot.
        size_t r = read.load(std::memory_order_relaxed);
        const size_t w = write.load(std::memory_order_acquire);
        while (r != w) {
            if (historyCount == history.size()) {
                history[historyStart] = queue[r];
                historyStart = (historyStart + 1) % history.size();
                ++discarded;
            } else {
                history[(historyStart + historyCount) % history.size()] = queue[r];
                ++historyCount;
            }
            r = (r + 1) % queue.size();
        }
        read.store(r, std::memory_order_release);
    }
};

AudioTraceRecorder::AudioTraceRecorder(size_t queueCapacity, size_t historyCapacity)
    : impl_(new Impl(queueCapacity, historyCapacity)) {}
AudioTraceRecorder::~AudioTraceRecorder() { Stop(); }
void AudioTraceRecorder::Start() {
    if (impl_->running.exchange(true)) return;
    try {
        impl_->consumer = std::thread([this] {
            while (impl_->running.load(std::memory_order_acquire)) {
                { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->Drain(); }
                std::unique_lock<std::mutex> waitLock(impl_->waitMutex);
                impl_->wake.wait_for(waitLock, std::chrono::milliseconds(20), [this] {
                    return !impl_->running.load(std::memory_order_acquire);
                });
            }
            std::lock_guard<std::mutex> lock(impl_->mutex); impl_->Drain();
        });
    } catch (...) { impl_->running.store(false); throw; }
}
void AudioTraceRecorder::Stop() {
    impl_->running.store(false, std::memory_order_release);
    impl_->wake.notify_one();
    if (impl_->consumer.joinable()) impl_->consumer.join();
}
bool AudioTraceRecorder::Push(AudioTraceEvent event) noexcept {
    event.sequence = ++impl_->sequence;
    const size_t w = impl_->write.load(std::memory_order_relaxed);
    const size_t next = (w + 1) % impl_->queue.size();
    if (next == impl_->read.load(std::memory_order_acquire)) {
        impl_->dropped.fetch_add(1, std::memory_order_relaxed); return false;
    }
    impl_->queue[w] = event;
    impl_->write.store(next, std::memory_order_release);
    return true;
}
uint64_t AudioTraceRecorder::Dropped() const noexcept { return impl_->dropped.load(std::memory_order_relaxed); }
AudioTraceSnapshot AudioTraceRecorder::Snapshot() {
    AudioTraceSnapshot result;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->Drain();
    result.dropped = Dropped(); result.historyDiscarded = impl_->discarded;
    result.events.reserve(impl_->historyCount);
    for (size_t i = 0; i < impl_->historyCount; ++i)
        result.events.push_back(impl_->history[(impl_->historyStart + i) % impl_->history.size()]);
    return result;
}
bool AudioTraceRecorder::ExportCsv(const std::filesystem::path& path, std::string& error) {
    const auto snapshot = Snapshot();
    // A failed export must not truncate the user's previous diagnostic file.
    // A uniquely created directory reserves the temporary filename atomically.
    static std::atomic<uint64_t> nonce{0};
    std::filesystem::path directory;
    std::error_code ec;
    bool reserved = false;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        directory = path;
        directory += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(nonce++);
        if (std::filesystem::create_directory(directory, ec)) { reserved = true; break; }
        if (ec && ec != std::errc::file_exists) break;
    }
    if (!reserved) { error = "Could not create temporary trace output"; return false; }
    const auto temporary = directory / "trace.csv";
    auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored); std::filesystem::remove(directory, ignored);
    };
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) { cleanup(); error = "Could not open trace output"; return false; }
    if (!WriteAudioTraceCsv(out, snapshot, error)) { out.close(); cleanup(); return false; }
    out.close();
    if (!out) { cleanup(); error = "Could not finish trace output"; return false; }
#ifdef _WIN32
    const bool replaced = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, path, ec);
    const bool replaced = !ec;
#endif
    cleanup();
    if (!replaced) { error = "Could not replace trace output"; return false; }
    return true;
}

namespace {
constexpr const char* Columns = "sequence,now,session,kind,flags,position,qpc,frequency,frames,fill,padding,want,missing_frames,rebuffer_frames,dt,packet_interval,error_seconds,headroom_seconds,clock_ppm,ppm,ratio,confidence,work_seconds,state,recovery,source_rate,render_rate,fifo_target_frames";
constexpr uint32_t KnownFlags = TraceDiscontinuity | TraceSilent | TraceTimestampError |
    TraceResetClocks | TraceResetController | TraceClearRecovery | TraceResetWatchdog |
    TraceReprime | TraceNewSession | TraceDriftEnabled | TraceStalled | TraceRestartCapture |
    TraceWatchdogRestarted | TraceObserveRecovery | TraceStartupSilence;
bool Bounded(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}
bool ValidEvent(const AudioTraceEvent& e) {
    const auto kind = static_cast<uint32_t>(e.kind);
    const auto rate = [](uint32_t r) { return !r || (r >= 8000 && r <= 768000); };
    if ((e.kind == AudioTraceKind::Reset && e.state > 4) ||
        (e.kind == AudioTraceKind::ClockEstimate && e.state > 3) ||
        (e.kind == AudioTraceKind::Control && (!e.sourceRate || !e.renderRate || e.ratio <= 0)) ||
        (e.kind == AudioTraceKind::RenderResult &&
            (uint64_t(e.frames) + e.missingFrames + e.rebufferFrames != e.want))) return false;
    return e.sequence && kind >= 1 && kind <= 8 && !(e.flags & ~KnownFlags) &&
        Bounded(e.now, 0, 1e12) && Bounded(e.frequency, 0, 1e12) &&
        e.frames <= 10000000 && e.fill <= 10000000 && e.padding <= 10000000 &&
        e.want <= 10000000 && e.missingFrames <= 10000000 && e.rebufferFrames <= 10000000 &&
        Bounded(e.dt, 0, 1e9) && Bounded(e.packetInterval, 0, 1e9) &&
        Bounded(e.errorSeconds, -3600, 3600) && Bounded(e.headroomSeconds, -3600, 3600) &&
        Bounded(e.clockPpm, -1000000, 1000000) && Bounded(e.ppm, -1000000, 1000000) &&
        Bounded(e.ratio, 0, 128) && Bounded(e.confidence, 0, 1) &&
        Bounded(e.workSeconds, 0, 3600) && e.state <= 16 && e.recovery <= 3 &&
        rate(e.sourceRate) && rate(e.renderRate) && e.fifoTargetFrames <= 10000000;
}
bool Validate(const AudioTraceSnapshot& trace, std::string& error) {
    if (trace.events.size() > AudioTraceMaxEvents) { error = "Trace event limit exceeded"; return false; }
    uint64_t sequence = 0; double now = 0;
    for (size_t i = 0; i < trace.events.size(); ++i) {
        const auto& e = trace.events[i];
        if (!ValidEvent(e) || e.sequence <= sequence || e.now < now) {
            error = "Invalid numeric value or event order at record " + std::to_string(i + 1); return false;
        }
        sequence = e.sequence; now = e.now;
    }
    return true;
}
template<class T> bool Number(const std::string& token, T& value) {
    if (token.empty() || token.size() > 64 || token.front() == '+' ||
        (std::is_unsigned<T>::value && token.front() == '-')) return false;
    std::istringstream input(token); input.imbue(std::locale::classic());
    input >> std::noskipws >> value;
    return input && input.peek() == std::char_traits<char>::eof();
}
std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> fields; size_t start = 0;
    for (;;) {
        const auto end = line.find(',', start);
        fields.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}
bool ParseEvent(const std::vector<std::string>& f, AudioTraceEvent& e) {
    if (f.size() != 28) return false;
    uint32_t kind = 0;
    const bool ok = Number(f[0],e.sequence) && Number(f[1],e.now) && Number(f[2],e.session) &&
        Number(f[3],kind) && Number(f[4],e.flags) && Number(f[5],e.position) && Number(f[6],e.qpc) &&
        Number(f[7],e.frequency) && Number(f[8],e.frames) && Number(f[9],e.fill) &&
        Number(f[10],e.padding) && Number(f[11],e.want) && Number(f[12],e.missingFrames) &&
        Number(f[13],e.rebufferFrames) && Number(f[14],e.dt) && Number(f[15],e.packetInterval) &&
        Number(f[16],e.errorSeconds) && Number(f[17],e.headroomSeconds) && Number(f[18],e.clockPpm) &&
        Number(f[19],e.ppm) && Number(f[20],e.ratio) && Number(f[21],e.confidence) &&
        Number(f[22],e.workSeconds) && Number(f[23],e.state) && Number(f[24],e.recovery) &&
        Number(f[25],e.sourceRate) && Number(f[26],e.renderRate) && Number(f[27],e.fifoTargetFrames);
    e.kind = static_cast<AudioTraceKind>(kind); return ok;
}
}

bool WriteAudioTraceCsv(std::ostream& out, const AudioTraceSnapshot& trace, std::string& error) {
    error.clear(); if (!Validate(trace, error)) return false;
    out.imbue(std::locale::classic()); out << std::setprecision(17);
    out << "nitlink_audio_trace,1," << trace.dropped << ',' << trace.historyDiscarded << '\n' << Columns << '\n';
    size_t bytes = 0;
    for (const auto& e : trace.events) {
        std::ostringstream row; row.imbue(std::locale::classic()); row << std::setprecision(17);
        row << e.sequence << ',' << e.now << ',' << e.session << ',' << static_cast<uint32_t>(e.kind) << ','
            << e.flags << ',' << e.position << ',' << e.qpc << ',' << e.frequency << ',' << e.frames << ','
            << e.fill << ',' << e.padding << ',' << e.want << ',' << e.missingFrames << ',' << e.rebufferFrames << ','
            << e.dt << ',' << e.packetInterval << ',' << e.errorSeconds << ',' << e.headroomSeconds << ','
            << e.clockPpm << ',' << e.ppm << ',' << e.ratio << ',' << e.confidence << ',' << e.workSeconds << ','
            << e.state << ',' << e.recovery << ',' << e.sourceRate << ',' << e.renderRate << ',' << e.fifoTargetFrames << '\n';
        const auto text = row.str(); bytes += text.size();
        if (bytes > AudioTraceMaxBytes - 1024) { error = "Trace byte limit exceeded"; return false; }
        out << text;
        if (!out) { error = "Trace output failed"; return false; }
    }
    if (!out) { error = "Trace output failed"; return false; }
    return true;
}
bool ReadAudioTraceCsv(std::istream& in, AudioTraceSnapshot& trace, std::string& error) {
    trace = {}; error.clear(); AudioTraceSnapshot parsed;
    size_t bytes = 0, lineNumber = 0;
    auto line = [&](std::string& result) {
        // getline into a fixed buffer rejects huge lines before allocating them.
        std::array<char, 2048> buffer{};
        in.getline(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = in.gcount();
        if (!in && !in.eof()) { error = "Trace line exceeds size limit or input failed"; return false; }
        if (read == 0) return false;
        bytes += static_cast<size_t>(read); ++lineNumber;
        if (bytes > AudioTraceMaxBytes) { error = "Trace byte limit exceeded"; return false; }
        const size_t length = static_cast<size_t>(read) - (in.eof() ? 0 : 1);
        result.assign(buffer.data(), length);
        if (!result.empty() && result.back() == '\r') result.pop_back();
        return true;
    };
    std::string value;
    if (!line(value)) { if(error.empty()) error = "Missing trace header"; return false; }
    const auto header = Split(value);
    if (header.size() != 4 || header[0] != "nitlink_audio_trace" || header[1] != "1" ||
        !Number(header[2], parsed.dropped) || !Number(header[3], parsed.historyDiscarded)) {
        error = "Unsupported trace version or malformed header"; return false;
    }
    if (!line(value) || value != Columns) { error = "Unexpected trace columns"; return false; }
    uint64_t sequence = 0; double now = 0;
    while (line(value)) {
        if (parsed.events.size() >= AudioTraceMaxEvents) { error = "Trace event limit exceeded"; return false; }
        AudioTraceEvent e;
        if (!ParseEvent(Split(value), e) || !ValidEvent(e) || e.sequence <= sequence || e.now < now) {
            error = "Invalid numeric value or event order at line " + std::to_string(lineNumber); return false;
        }
        sequence = e.sequence; now = e.now; parsed.events.push_back(e);
    }
    if (!error.empty()) return false;
    trace = std::move(parsed); return true;
}

AudioReplayReport ReplayAudioTrace(const AudioTraceSnapshot& trace) {
    AudioReplayReport report; report.events = trace.events.size();
    std::string validation;
    if (!Validate(trace, validation)) {
        report.complete = false; report.mismatches = 1; report.details.push_back(validation); return report;
    }
    AudioClockRate capture, render;
    AudioClockBlend blend;
    DriftController drift;
    AudioRecoveryGuard recovery;
    CaptureProgressWatchdog watchdog;
    double clockPpm = 0; uint32_t sourceRate = 0, renderRate = 0;
    bool synchronized = false; uint64_t previousSequence = 0;
    auto detail = [&](const std::string& message) { if (report.details.size() < 24) report.details.push_back(message); };
    if (trace.dropped || trace.historyDiscarded) {
        report.complete = false;
        detail("Trace reports " + std::to_string(trace.dropped) + " queue drops and " +
            std::to_string(trace.historyDiscarded) + " discarded history records; gaps/prefix cannot be reconstructed.");
    }
    if (!trace.events.empty() && trace.events.front().sequence != 1) {
        report.complete = false;
        detail("The first sequence is not 1; this file contains only a suffix of the recording.");
    }
    for (const auto& e : trace.events) {
        if (previousSequence && e.sequence != previousSequence + 1) {
            ++report.sequenceGaps; synchronized = false; report.complete = false;
            detail("Sequence gap before " + std::to_string(e.sequence) + ": policy state is unknown until a new session reset.");
        }
        previousSequence = e.sequence;
        if (e.sourceRate) sourceRate = e.sourceRate;
        if (e.renderRate) renderRate = e.renderRate;
        auto check = [&](bool mismatch, const std::string& why) {
            ++report.decisions;
            if (!synchronized) { ++report.unverifiable; report.complete = false; return; }
            ++report.checked;
            if (mismatch) { ++report.mismatches; detail("Sequence " + std::to_string(e.sequence) + ": " + why); }
        };
        switch (e.kind) {
        case AudioTraceKind::Reset:
            ++report.resets;
            if (e.flags & TraceNewSession) {
                capture.Reset(); render.Reset(); blend.Reset(); drift.Reset();
                recovery = AudioRecoveryGuard{}; watchdog.Reset(e.now); clockPpm = 0;
                synchronized = true;
            }
            if (e.flags & TraceResetClocks) { capture.Reset(); render.Reset(); blend.Reset(); clockPpm = 0; }
            if (e.flags & TraceResetController) drift.Reset();
            if (e.flags & TraceClearRecovery) recovery.ClearWindow();
            if (e.flags & TraceResetWatchdog) watchdog.Reset(e.now);
            if (e.flags & TraceWatchdogRestarted) watchdog.EndpointRestarted(e.now);
            if (e.flags & TraceReprime) drift.Reprime(static_cast<DriftResetReason>(e.state));
            break;
        case AudioTraceKind::Capture:
            watchdog.Progress(e.now, e.position, e.frames);
            if (e.flags & (TraceDiscontinuity | TraceTimestampError)) capture.RejectTimestamp(e.now);
            else if (e.frames) capture.Sample(e.position, e.qpc, e.frequency ? e.frequency : sourceRate, e.now);
            break;
        case AudioTraceKind::RenderClock:
            if (e.flags & TraceTimestampError) render.RejectTimestamp(e.now);
            else render.Sample(e.position, e.qpc, e.frequency, e.now);
            break;
        case AudioTraceKind::ClockEstimate: {
            const auto estimate = blend.Update(capture, render, e.now); clockPpm = estimate.ppm;
            const double difference = std::abs(estimate.ppm - e.clockPpm);
            if (synchronized) report.maxClockDifferencePpm = std::max(report.maxClockDifferencePpm, difference);
            check(difference > .001 || std::abs(estimate.confidence - e.confidence) > 1e-6 ||
                static_cast<uint32_t>(estimate.state) != e.state,
                "clock estimate differs; inspect timestamp rejection, reset ordering or estimator version");
            break;
        }
        case AudioTraceKind::Control: {
            const double ppm = (e.flags & TraceDriftEnabled)
                ? drift.Update(e.errorSeconds, e.dt, clockPpm, e.headroomSeconds) : 0;
            const double difference = std::abs(ppm - e.ppm);
            const double ratio = renderRate ? double(sourceRate) / renderRate * (1 + ppm / 1e6) : e.ratio;
            if (synchronized) report.maxControlDifferencePpm = std::max(report.maxControlDifferencePpm, difference);
            check(difference > .001 || std::abs(clockPpm - e.clockPpm) > .001 || std::abs(ratio - e.ratio) > 1e-10,
                "drift decision differs; inspect dt/headroom, enable state, reset reason or controller version");
            break;
        }
        case AudioTraceKind::RenderResult: {
            report.missingFrames += e.missingFrames;
            if (e.flags & TraceStartupSilence) report.startupSilenceFrames += e.rebufferFrames;
            else report.rebufferFrames += e.rebufferFrames;
            if (e.flags & TraceClearRecovery) recovery.ClearWindow();
            if (!(e.flags & TraceObserveRecovery)) break;
            const auto action = recovery.Decide(e.now, e.errorSeconds,
                renderRate ? double(e.missingFrames) / renderRate : 0,
                renderRate ? double(e.want) / renderRate : 0);
            check(static_cast<uint32_t>(action) != e.recovery, "recovery action differs; inspect missing frames, sliding window and cooldown resets");
            break;
        }
        case AudioTraceKind::Watchdog: {
            const auto status = watchdog.Observe(e.now);
            check(status.stalled != bool(e.flags & TraceStalled) || status.restartCapture != bool(e.flags & TraceRestartCapture),
                "capture watchdog differs; inspect capture progress and reset ordering");
            break;
        }
        case AudioTraceKind::Timing: break;
        }
    }
    if (report.unverifiable) detail(std::to_string(report.unverifiable) + " decisions have unknown initial state; export from a new session to verify them.");
    if (!report.decisions) {
        report.complete = false;
        detail("No control, clock, recovery or watchdog decisions were recorded; nothing could be verified.");
    }
    return report;
}
void PrintAudioReplayReport(std::ostream& out, const AudioReplayReport& r) {
    out << "Audio trace replay: " << r.events << " events, " << r.checked << '/' << r.decisions
        << " decisions checked, " << r.mismatches << " mismatches, " << r.unverifiable << " unverifiable.\n"
        << "Coverage: " << (r.complete ? "complete" : "incomplete") << "; sequence gaps: " << r.sequenceGaps
        << "; resets: " << r.resets << "; missing frames: " << r.missingFrames
        << "; rebuffer silence: " << r.rebufferFrames << "; startup silence: " << r.startupSilenceFrames << " frames.\n"
        << "Maximum clock/control difference: " << r.maxClockDifferencePpm << " / " << r.maxControlDifferencePpm << " ppm.\n";
    for (const auto& detail : r.details) out << "- " << detail << '\n';
}
}
