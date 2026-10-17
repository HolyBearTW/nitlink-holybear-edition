#include "capture/elgato_4kx_source.h"
#include "app/capture_output_policy.h"

#include <array>
#include <iostream>
#include <stdexcept>

using namespace NitLink;

namespace {
unsigned checks = 0;
void Check(bool ok, const char* label)
{
    if (!ok) throw std::runtime_error(label);
    ++checks;
}
Source4KProMode Probe(bool valid, bool hdr)
{
    Source4KProMode result;
    result.detected = true;
    result.width = 2560;
    result.height = 1440;
    result.fps = 60;
    result.hdrValid = valid;
    result.hdrActive = hdr;
    return result;
}
Elgato4KXCapturePolicy Policy(const Source4KProMode& mode, bool output,
                             CaptureFormatPreference preference,
                             NegotiatedCaptureFormatKind actual,
                             bool known = true, bool fallback = false)
{
    bool source = false, available = false;
    Apply4KXSourceHdr(mode, source, available);
    return Decide4KXSourceCapturePolicy(actual, known, output, preference,
                                      source, available, fallback);
}
}

int main()
{
    try {
        Source4KProMode startupHDR;
        Check(Merge4KXSourceMode(startupHDR, Probe(true, true)),
              "HDR already active at startup publishes its first valid state");
        const auto firstHDR = Policy(startupHDR, false, CaptureFormatPreference::Auto,
                                     NegotiatedCaptureFormatKind::NV12);
        Check(firstHDR.desiredCaptureIsP010 && firstHDR.reopenCapture,
              "first valid HDR after startup promotes SDR capture to P010");
        Source4KProMode cache;
        bool source = true, available = true;
        Apply4KXSourceHdr(cache, source, available);
        Check(!source && !available, "new selected device starts unknown");
        Check(Merge4KXSourceMode(cache, Probe(false, false)),
              "first timing sample publishes without fabricating HDR validity");
        Check(!cache.hdrValid, "timing success alone does not establish SDR");
        for (bool output : {false, true}) {
            const auto startup = Policy(cache, output, CaptureFormatPreference::Auto,
                                        NegotiatedCaptureFormatKind::NV12);
            Check(startup.desiredCaptureIsP010 == output,
                  "unknown source retains the output preference startup fallback");
        }
        Check(Merge4KXSourceMode(cache, Probe(true, false)),
              "first valid SDR at identical timing is a published change");
        Apply4KXSourceHdr(cache, source, available);
        Check(available && !source, "valid SDR clears unavailable state");
        Check(Merge4KXSourceMode(cache, Probe(true, true)),
              "SDR to HDR at identical timing is a published change");
        Apply4KXSourceHdr(cache, source, available);
        Check(source && available, "HDR feeds capture policy and settings availability");
        for (bool output : {false, true}) {
            const auto hdr = Policy(cache, output, CaptureFormatPreference::Auto,
                                    NegotiatedCaptureFormatKind::NV12);
            Check(hdr.desiredCaptureIsP010 && hdr.reopenCapture,
                  "valid HDR promotes Auto to P010 with either output preference");
            const auto settled = Policy(cache, output, CaptureFormatPreference::Auto,
                                        NegotiatedCaptureFormatKind::P010);
            Check(!settled.reopenCapture, "negotiated P010 needs no reopen");
        }
        Check(!Merge4KXSourceMode(cache, Probe(false, false)),
              "transient failed HDR query at identical timing publishes no false SDR");
        Check(cache.hdrValid && cache.hdrActive, "last valid HDR survives query failure");
        auto timingChanged = Probe(false, false);
        timingChanged.width = 1920;
        timingChanged.height = 1080;
        Check(Merge4KXSourceMode(cache, timingChanged), "timing change still publishes");
        Check(cache.width == 1920 && cache.hdrValid && cache.hdrActive,
              "timing update preserves the last independently valid HDR result");
        auto noSignal = Probe(true, false);
        noSignal.detected = false;
        Check(!Merge4KXSourceMode(cache, noSignal), "unlocked timing cannot replace source state");
        Check(cache.hdrValid && cache.hdrActive, "signal loss retains last valid HDR");
        Check(Merge4KXSourceMode(cache, Probe(true, false)), "reacquired valid SDR publishes");
        for (bool output : {false, true}) {
            const auto sdr = Policy(cache, output, CaptureFormatPreference::Auto,
                                    NegotiatedCaptureFormatKind::P010);
            Check(!sdr.desiredCaptureIsP010 && sdr.reopenCapture,
                  "valid SDR changes capture back with either output preference");
        }
        const auto priorDevice = cache;
        cache = {};
        Apply4KXSourceHdr(cache, source, available);
        Check(!source && !available, "device switch clears detection and source state");
        Merge4KXSourceMode(cache, Probe(false, false));
        Check(!cache.hdrValid, "failed first query cannot inherit another card's SDR validity");
        cache = Probe(true, true);
        cache = {};
        Merge4KXSourceMode(cache, Probe(false, false));
        Check(!cache.hdrValid && !cache.hdrActive, "new device cannot inherit another card's HDR");
        cache = priorDevice;
        Check(cache.hdrValid && !cache.hdrActive, "rollback restores selected-device snapshot");

        for (bool hdr : {false, true}) {
            cache = Probe(true, hdr);
            for (bool output : {false, true}) {
                for (auto preference : {CaptureFormatPreference::ManualNV12,
                                        CaptureFormatPreference::ManualOther,
                                        CaptureFormatPreference::ManualP010}) {
                    const auto selected = Policy(cache, output, preference,
                                                 NegotiatedCaptureFormatKind::NV12);
                    Check(selected.desiredCaptureIsP010 ==
                              (preference == CaptureFormatPreference::ManualP010),
                          "manual format wins over source and output preferences");
                }
            }
        }
        cache = Probe(true, true);
        NonGcP010FallbackState fallback;
        fallback.CompleteOpen(true, false);
        for (auto preference : {CaptureFormatPreference::Auto,
                                CaptureFormatPreference::ManualP010}) {
            const auto rejected = Policy(cache, true, preference,
                                         NegotiatedCaptureFormatKind::NV12,
                                         true, fallback.Accepted());
            Check(rejected.desiredCaptureIsP010 && !rejected.reopenCapture,
                  "running failed P010 negotiation remains accepted without retry loop");
            const auto differentDevice = Policy(cache, true, preference,
                                                NegotiatedCaptureFormatKind::NV12,
                                                false, fallback.Accepted());
            Check(differentDevice.reopenCapture, "fallback belongs only to the current device");
        }
        fallback.ObservePolicy(true, true, true);
        Check(Policy(cache, true, CaptureFormatPreference::Auto,
                     NegotiatedCaptureFormatKind::NV12, true,
                     fallback.Accepted()).reopenCapture,
              "explicit reopen retries P010 after fallback");
        fallback.CompleteOpen(true, false);
        fallback.ObservePolicy(false, true, false);
        Check(!fallback.Accepted(), "SDR source transition clears prior P010 rejection");

        Source4KProMode decoded;
        const std::array<uint8_t, 5> hdrReply{0, 0x22, 0, 0, 0x87};
        const std::array<uint8_t, 5> sdrReply{0, 0x22, 0, 0, 0};
        const std::array<uint8_t, 5> wrongReply{0, 0x2a, 0, 0, 0};
        Decode4KXHdrInfoFrame(decoded, hdrReply);
        Check(decoded.hdrValid && decoded.hdrActive && !decoded.detected,
              "HDR validity is independent of timing read validity");
        Decode4KXHdrInfoFrame(decoded, std::span(hdrReply).first(4));
        Check(!decoded.hdrValid, "truncated HDR reply is unavailable");
        Decode4KXHdrInfoFrame(decoded, wrongReply);
        Check(!decoded.hdrValid, "wrong mailbox reply is unavailable");
        Decode4KXHdrInfoFrame(decoded, {});
        Check(!decoded.hdrValid, "absent HDR reply is unavailable");
        Decode4KXHdrInfoFrame(decoded, sdrReply);
        Check(decoded.hdrValid && !decoded.hdrActive, "valid SDR reply establishes SDR");
        std::cout << "PASS " << checks << " 4K X HDR checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
