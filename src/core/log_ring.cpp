// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/log_ring.h"

#include <algorithm>
#include <utility>

namespace coney::logging {

LogRing::LogRing(std::uint64_t keepSteps, std::size_t maxRecords)
    : m_keepSteps(keepSteps), m_maxRecords(std::max<std::size_t>(maxRecords, 1)) {}

void LogRing::add(Record record) {
    const std::uint64_t newest = record.step;
    m_records.push_back(std::move(record));
    // Older than the window: gone. The steps only grow, so the oldest are at the front; the new record itself is never
    // older than the window, so the loop stops at it.
    while (m_records.front().step + m_keepSteps < newest) {
        m_records.pop_front();
        ++m_dropped;
    }
    while (m_records.size() > m_maxRecords) {
        m_records.pop_front();
        ++m_dropped;
    }
}

void LogRing::clear() {
    m_records.clear();
    m_dropped = 0;
}

} // namespace coney::logging
