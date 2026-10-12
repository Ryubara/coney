// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "core/log_record.h"

namespace coney::logging {

/// Which lines reach the log's outputs: Info and up in every category, Detail too in the verbose ones
/// (`--log-verbose ai,combat`). The ring keeps every line whatever the filter says. Coney's own;
/// docs/guides/run-log.md.
class LogFilter {
  public:
    /// Whether a line of `level` in `category` passes.
    [[nodiscard]] bool passes(Category category, Level level) const {
        return static_cast<int>(level) <= static_cast<int>(threshold(category));
    }
    /// Lets Detail lines of `category` through.
    void setVerbose(Category category) { m_thresholds.at(index(category)) = Level::Detail; }
    /// Whether `category`'s Detail lines pass.
    [[nodiscard]] bool verbose(Category category) const { return threshold(category) == Level::Detail; }
    /// The least important level that passes in `category`.
    [[nodiscard]] Level threshold(Category category) const { return m_thresholds.at(index(category)); }

  private:
    // The array index of `category`.
    static std::size_t index(Category category) { return static_cast<std::size_t>(category); }

    std::array<Level, kCategoryCount> m_thresholds{Level::Info, Level::Info, Level::Info,
                                                   Level::Info, Level::Info, Level::Info};
};

} // namespace coney::logging
