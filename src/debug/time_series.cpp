// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/time_series.h"

#include <algorithm>

#include "core/assert.h"

namespace coney::debug {

TimeSeries::TimeSeries(std::size_t capacity) : m_ring(std::max<std::size_t>(capacity, 1), 0.0F) {}

void TimeSeries::push(float value) {
    m_ring[m_next] = value;
    m_next = (m_next + 1) % m_ring.size();
    m_count = std::min(m_count + 1, m_ring.size());
}

float TimeSeries::at(std::size_t index) const {
    CONEY_ASSERT(index < m_count);
    // The oldest sample sits where the next one will go once the ring is full, at 0 before.
    const std::size_t oldest = m_count == m_ring.size() ? m_next : 0;
    return m_ring[(oldest + index) % m_ring.size()];
}

std::vector<float> TimeSeries::values() const {
    std::vector<float> result;
    result.reserve(m_count);
    for (std::size_t i = 0; i < m_count; ++i) {
        result.push_back(at(i));
    }
    return result;
}

} // namespace coney::debug
