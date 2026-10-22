// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include "core/log_record.h"

namespace coney::logging {

/// The last stretch of the log at every level, in memory, for a bug report or a crash: written into the log file
/// when an assertion fails, the process crashes, or the debug menus ask for a bug report (logging::dump()). Bounded
/// twice: by steps (records older than the newest by more than keepSteps go) and by count, so a flood of lines in a
/// few steps cannot grow it without end. Coney's own; docs/guides/run-log.md#ring.
class LogRing {
  public:
    /// 30 s of play at 30 steps a second.
    static constexpr std::uint64_t kDefaultSteps = 900;
    /// Far more than 30 s of ordinary play writes; a cap for floods.
    static constexpr std::size_t kDefaultMaxRecords = 20'000;

    /// A ring keeping `keepSteps` steps before the newest record's and at most `maxRecords` records (at least 1).
    explicit LogRing(std::uint64_t keepSteps = kDefaultSteps, std::size_t maxRecords = kDefaultMaxRecords);

    /// Adds `record` (its step at least the last one's) and forgets what falls out of the bounds.
    void add(Record record);
    /// The records kept, oldest first.
    [[nodiscard]] const std::deque<Record>& records() const { return m_records; }
    /// How many are kept.
    [[nodiscard]] std::size_t size() const { return m_records.size(); }
    /// How many were forgotten so far.
    [[nodiscard]] std::uint64_t dropped() const { return m_dropped; }
    /// Forgets every record (and the count of dropped ones).
    void clear();

  private:
    std::deque<Record> m_records;
    std::uint64_t m_keepSteps;
    std::size_t m_maxRecords;
    std::uint64_t m_dropped = 0;
};

} // namespace coney::logging
