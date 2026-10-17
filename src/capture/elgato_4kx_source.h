#pragma once

#include "elgato_hdr_control.h"
#include <span>

namespace NitLink {

// A missing or malformed mailbox reply cannot establish an SDR transition.
inline void Decode4KXHdrInfoFrame(Source4KProMode& result,
                                  std::span<const uint8_t> reply)
{
    result.hdrValid = reply.size() >= 5 && reply[1] == 0x22;
    if (result.hdrValid) result.hdrActive = reply[4] == 0x87;
}

// Timing and HDR queries can fail independently during signal transitions.
// Only a locked timing sample may replace the mode; an unavailable HDR query
// retains the last valid state within this poller's selected-device lifetime.
inline bool Merge4KXSourceMode(Source4KProMode& current,
                               const Source4KProMode& probe)
{
    if (!probe.detected) return false;
    auto next = probe;
    if (!probe.hdrValid) {
        next.hdrValid = current.hdrValid;
        next.hdrActive = current.hdrActive;
    }
    const bool changed = !current.detected ||
        next.width != current.width || next.height != current.height ||
        next.fps != current.fps || next.hdrValid != current.hdrValid ||
        next.hdrActive != current.hdrActive ||
        next.sourceName != current.sourceName;
    if (changed) current = next;
    return changed;
}

// The selected-device cache supplies both policy and settings availability.
// Capture negotiation and the HDR output preference are separate inputs.
inline void Apply4KXSourceHdr(const Source4KProMode& mode,
                              bool& sourceIsHDR10, bool& detectionAvailable)
{
    detectionAvailable = mode.hdrValid;
    sourceIsHDR10 = mode.hdrValid && mode.hdrActive;
}

} // namespace NitLink
