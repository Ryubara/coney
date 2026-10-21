// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "core/error.h"

namespace coney {
struct Options;
}

namespace coney::debug {
class PlayControls;
}

namespace coney::platform {

/// `--bench`: a measured `--play-level` run (docs/guides/performance.md). On the level's first frame of play it
/// spawns `--bench-fighters` AI fighters in front of the player, as the debug menus' AI fighters page does; after
/// `--bench-warmup` frames it starts recording every frame's profile (core/perf.h); at the end it writes the summary
/// as JSON. The run itself is the usual test-mode run: lockstep, scripted, the same steps every time, so two builds
/// measured with the same command did the same work. Coney's own debug tooling.
class PerfBench {
  public:
    /// A bench from the parsed options (benchFile set); turns the profiler on, with its clock and counters.
    explicit PerfBench(const Options& options);

    /// Called once a frame from the level's first frame of play on, with that frame's number (0 first) and the play
    /// mode's controls (null while there is none: the fighters are then not spawned, and the report says so).
    void playFrame(std::uint64_t frame, debug::PlayControls* play);

    /// Stops recording, writes the JSON report with `meta` (what was run: the level, the build, the renderer) added
    /// to the bench's own, and returns a one-line summary for the console.
    std::expected<std::string, Error> finish(std::vector<std::pair<std::string, std::string>> meta);

  private:
    std::string m_file;
    int m_fighters = 0;
    std::uint64_t m_warmup = 0;
    std::uint64_t m_frames = 0;
    std::size_t m_spawned = 0;
    std::string m_spawnError; // why fewer fighters than asked were spawned
};

} // namespace coney::platform
