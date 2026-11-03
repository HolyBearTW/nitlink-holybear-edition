#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace NitLink {
// Numeric metadata only. Never put samples, endpoint names or identifiers here.
enum class AudioTraceKind : uint32_t {
    Reset = 1, Capture, RenderClock, ClockEstimate, Control, RenderResult, Watchdog, Timing
};
enum AudioTraceFlags : uint32_t {
    TraceDiscontinuity = 1, TraceSilent = 2, TraceTimestampError = 4,
    TraceResetClocks = 1u << 8, TraceResetController = 1u << 9,
    TraceClearRecovery = 1u << 10, TraceResetWatchdog = 1u << 11,
    TraceReprime = 1u << 12, TraceNewSession = 1u << 13,
    TraceDriftEnabled = 1u << 16, TraceStalled = 1u << 17,
    TraceRestartCapture = 1u << 18, TraceWatchdogRestarted = 1u << 19,
    TraceObserveRecovery = 1u << 20, TraceStartupSilence = 1u << 21
};
struct AudioTraceEvent {
    uint64_t sequence = 0; // Assigned by Push, including failed pushes.
    double now = 0;       // Steady-clock seconds, nondecreasing within this file.
    uint32_t session = 0;
    AudioTraceKind kind = AudioTraceKind::Timing;
    uint32_t flags = 0;
    uint64_t position = 0, qpc = 0; // Device position and associated QPC in 100 ns.
    double frequency = 0;
    uint32_t frames = 0, fill = 0, padding = 0, want = 0;
    uint32_t missingFrames = 0, rebufferFrames = 0;
    double dt = 0, packetInterval = 0, errorSeconds = 0, headroomSeconds = 0;
    double clockPpm = 0, ppm = 0, ratio = 0, confidence = 0, workSeconds = 0;
    uint32_t state = 0, recovery = 0;
    uint32_t sourceRate = 0, renderRate = 0, fifoTargetFrames = 0;
};
// Reset.state is DriftResetReason. ClockEstimate.state is AudioClockState.
// Capture always calls watchdog.Progress; bad timestamps call RejectTimestamp.
// ClockEstimate corresponds to exactly one blend.Update, Control to one drift
// Update (when enabled), RenderResult with TraceObserveRecovery to one
// recovery.Decide (including zeros). Other RenderResult events only count audio.
// RenderResult.frames is written audio, and missing/rebuffer are separate.
// Record these events in call order; in particular, record Control before an
// underrun's Reprime. Reset.NewSession starts all policies from a clean state.
struct AudioTraceSnapshot {
    uint64_t dropped = 0, historyDiscarded = 0;
    std::vector<AudioTraceEvent> events;
};

// One producer (the audio worker). Start/Stop/Snapshot/Export are non-real-time.
// Push performs bounded copies/lock-free atomics, no allocation or notification.
// A separate consumer retains a bounded recent history. Snapshot serializes with
// that consumer, never with the producer, and drains outstanding events first.
class AudioTraceRecorder {
public:
    explicit AudioTraceRecorder(size_t queueCapacity = 8192, size_t historyCapacity = 65536);
    ~AudioTraceRecorder();
    AudioTraceRecorder(const AudioTraceRecorder&) = delete;
    AudioTraceRecorder& operator=(const AudioTraceRecorder&) = delete;
    void Start();
    void Stop();
    bool Push(AudioTraceEvent event) noexcept;
    uint64_t Dropped() const noexcept;
    AudioTraceSnapshot Snapshot();
    bool ExportCsv(const std::filesystem::path& path, std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

constexpr size_t AudioTraceMaxEvents = 250000;
constexpr size_t AudioTraceMaxBytes = 64 * 1024 * 1024;
bool WriteAudioTraceCsv(std::ostream& out, const AudioTraceSnapshot& trace, std::string& error);
bool ReadAudioTraceCsv(std::istream& in, AudioTraceSnapshot& trace, std::string& error);

struct AudioReplayReport {
    size_t events = 0, decisions = 0, checked = 0, mismatches = 0, unverifiable = 0;
    size_t sequenceGaps = 0, resets = 0;
    uint64_t missingFrames = 0, rebufferFrames = 0, startupSilenceFrames = 0;
    double maxClockDifferencePpm = 0, maxControlDifferencePpm = 0;
    bool complete = true;
    std::vector<std::string> details; // Bounded explanations, not one per sample.
};
// Re-runs the production clock estimators, drift controller, recovery guard and
// watchdog. A lost prefix/gap is not silently treated as a successful replay;
// strict comparison resumes only after a NewSession reset.
AudioReplayReport ReplayAudioTrace(const AudioTraceSnapshot& trace);
void PrintAudioReplayReport(std::ostream& out, const AudioReplayReport& report);
}
