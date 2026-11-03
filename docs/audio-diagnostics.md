# Audio diagnostics and deterministic replay

NitLink keeps the configured FIFO and render targets unchanged. Recovery first
re-primes locally, then rebuilds playback, and only escalates to both endpoints
when repeated damage warrants it. A capture endpoint which stops delivering
packets receives one retry after established progress. An absent HDMI signal does
not cause continuous reopen attempts. Silent capture packets count as progress.

The optional diagnostics panel distinguishes actual playback, initial priming,
rebuffering, capture stall, and recovery. Missing output frames count the failed
part of an active render request; startup and subsequent rebuffer silence have
separate counters. Reset discards are source frames, while silence counters are
output frames. Counts at different sample rates must not be added as durations.
Clock confidence reflects timestamp fit quality. A temporarily unavailable fit
holds the learned rate for one second, then fades over four seconds; a device
reset immediately invalidates it.

Volume, mute and resumed audio use a one-millisecond sample ramp without adding
queued audio. Gain is applied before the single output clipping/quantization.
The block converter decodes each needed input sample once and preserves the
existing sinc coefficients and fractional phase. Equal nominal sample rates with
drift correction disabled bypass the FIR; format/channel conversion and gain
remain active. Different nominal rates still require resampling.

## Export

Enable audio diagnostics in Settings, then export a CSV through the native save
dialog. The file contains numeric timing, queue and controller events, not PCM,
device names or identifiers. Recording uses a fixed single-producer queue; a
background consumer retains bounded recent history. Export never waits on the
audio worker. Queue overflow and overwritten history are reported explicitly.

Run the packaged utility from a terminal:

```
NitLinkAudioReplay.exe trace.csv
```

Replay runs the production clock fits, confidence blend, drift feedback, watchdog
and recovery logic in recorded call order. This checks recorded decisions, not
waveform fidelity. A truncated prefix or lost event makes dependent decisions
unverifiable until a complete new session reset. Export shortly after reproducing
a problem in a freshly started application for a complete decision comparison.
A trace is diagnostic evidence, not proof of a driver or hardware timing bound.

## Verification

Portable tests cover timestamp faults, confidence decay, recovery windows and
cooldowns, no-signal backoff, malformed trace input, queue overflow, control-loop
simulation, PCM conversion, clipping, gain transitions, ring wrap and split-block
phase continuity. The processing test checks that the hot path allocates no heap
memory. The benchmark compares the original per-tap decoder with block decoding.
The native tests exercise the real router against fake WASAPI endpoints. WebView
tests cover diagnostics visibility, localized state and native-only export.
`NitLinkAudioReplay.exe` and this guide are included by the package manifest.

Physical USB packet cadence, Windows driver behavior and audible transitions still
need testing on the target capture card and output device. Keep the same FIFO
setting when comparing builds, including the 3 ms configuration.
