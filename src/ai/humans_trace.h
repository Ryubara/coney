// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <fstream>
#include <memory>
#include <string>

#include "ai/fight_trace.h"
#include "core/error.h"

// The file of the all-humans trace (`--trace-humans FILE`, docs/guides/running-play-level.md#tracing), shared by the
// play modes a run loads one after another (a Rumble match goes through its menu and the arena's level), so they write
// one file with one step count and one numbering of the humans. Research: docs/research/feel.md#method

namespace coney::ai {

class Brains;

/// The open trace file, the tracer that numbers the humans and the steps traced.
class HumansTrace {
  public:
    /// Opens `path` (replacing it) and writes the header. Fails with ErrorCode::Io when it cannot be written.
    /// @coney-only the all-humans trace, a test and research aid; the original has none
    [[nodiscard]] static std::expected<std::shared_ptr<HumansTrace>, Error> open(const std::string& path);

    /// Writes one row per brain of `brains` for the next step.
    /// @coney-only the all-humans trace, a test and research aid; the original has none
    void write(const Brains& brains);
    /// The steps written so far.
    [[nodiscard]] std::uint64_t steps() const { return m_steps; }

  private:
    HumansTrace() = default;

    std::ofstream m_file;
    FightTracer m_tracer;
    std::uint64_t m_steps = 0;
};

} // namespace coney::ai
