#pragma once
#include <algorithm>
#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace NitLink {
// Bound native dispatch from a compromised or malfunctioning settings page.
// Slider values above this budget are coalesced by the host and drained later.
class MessageBudget {
public:
    bool Accept(uint64_t nowMs) {
        const uint64_t elapsed = nowMs >= m_lastMs ? std::min<uint64_t>(nowMs - m_lastMs, 2000) : 0;
        m_lastMs = nowMs;
        m_credit = std::min<uint64_t>(120000, m_credit + elapsed * 60);
        if (m_credit < 1000) return false;
        m_credit -= 1000;
        return true;
    }
private:
    uint64_t m_lastMs = 0;
    uint64_t m_credit = 120000;
};
// Validated slider payloads occupy fixed slots, independent of WebView2.
class PendingSliderMessages {
public:
    bool Store(size_t index, std::wstring_view json) {
        if (index >= m_values.size() || json.empty() || json.size() > 8192) return false;
        const bool replaced = !m_values[index].empty();
        m_values[index] = json;
        return replaced;
    }
    bool Has(size_t index) const { return index < m_values.size() && !m_values[index].empty(); }
    bool Empty() const { return !Has(0) && !Has(1) && !Has(2) && !Has(3) && !Has(4); }
    void Clear() { m_values = {}; }
    template<class Callback>
    void Drain(MessageBudget& budget, uint64_t now, bool closing, Callback&& dispatch) {
        const size_t first = m_next;
        for (size_t offset = 0; offset < m_values.size(); ++offset) {
            const size_t index = (first + offset) % m_values.size();
            if (!Has(index) || (!closing && !budget.Accept(now))) continue;
            auto json = std::move(m_values[index]);
            m_values[index].clear();
            m_next = (index + 1) % m_values.size();
            dispatch(json);
        }
    }
private:
    std::array<std::wstring, 5> m_values;
    size_t m_next = 0;
};
} // namespace NitLink
