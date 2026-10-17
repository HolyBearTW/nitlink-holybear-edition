#pragma once

#include <array>
#include <cstdint>

namespace NitLink {

// Comparison order carries the alternating source cadence, so ready results
// stay queued until each older result has been consumed or explicitly lost.
class FrameReadbackQueue {
public:
    static constexpr uint32_t kCapacity = 3;

    bool Empty() const { return m_count == 0; }
    bool Full() const { return m_count == kCapacity; }
    uint32_t Size() const { return m_count; }
    uint32_t ReadSlot() const { return m_head; }
    uint32_t WriteSlot() const { return (m_head + m_count) % kCapacity; }
    uint64_t ReadFrame() const { return m_frames[m_head]; }
    uint64_t LastConsumedFrame() const { return m_lastConsumed; }

    bool FrontHasGap() const {
        return !Empty() && m_lastConsumed != 0 && ReadFrame() != m_lastConsumed + 1;
    }

    bool Submit(uint64_t frame) {
        if (Full() || frame <= m_lastSubmitted) return false;
        m_frames[WriteSlot()] = frame;
        ++m_count;
        m_lastSubmitted = frame;
        return true;
    }

    bool Consume() {
        if (Empty()) return false;
        m_lastConsumed = ReadFrame();
        return Discard();
    }

    // Failed reads leave a gap for the next consumed result to report.
    bool Discard() {
        if (Empty()) return false;
        m_head = (m_head + 1) % kCapacity;
        --m_count;
        return true;
    }

    void Reset() { *this = FrameReadbackQueue{}; }

private:
    std::array<uint64_t, kCapacity> m_frames{};
    uint32_t m_head = 0;
    uint32_t m_count = 0;
    uint64_t m_lastSubmitted = 0;
    uint64_t m_lastConsumed = 0;
};

} // namespace NitLink
