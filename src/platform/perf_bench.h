// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
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

/// `--bench`: a measured `--play-level` run (docs/guides/performance.md). On the level's first frame of play (or once
/// `--bench-spawn-step` steps of play have run) it spawns `--bench-fighters` AI fighters in front of the player and
/// `--bench-allies` behind him, as the debug menus' AI fighters page does, and with `--bench-god` makes him invincible;
/// `--bench-warmup` frames later it starts recording every frame's profile (core/perf.h); at the end it writes the
/// summary as JSON, and with `--bench-frames` every frame as CSV. The run itself is the usual test-mode run: lockstep,
/// scripted, the same steps every time, so two builds measured with the same command did the same work
/// (`--bench-real-time` paces it as a player's run instead). Coney's own debug tooling.
class PerfBench {
  public:
    /// A bench from the parsed options (benchFile set); turns the profiler on, with its clock and counters.
    explicit PerfBench(const Options& options);

    /// Called once a frame from the level's first frame of play on, with that frame's number (0 first) and the play
    /// mode's controls (null while there is none: the fighters are then not spawned, and the report says so).
    void playFrame(std::uint64_t frame, debug::PlayControls* play);

    /// Whether `--bench-measure` frames are measured, so the run may end before `--frames`.
    [[nodiscard]] bool measured() const { return m_measured; }

    /// Counts one fixed step of play (the main loop's after-step hook), for `--bench-spawn-step`.
    void noteStep() { ++m_steps; }

    /// Stops recording, writes the JSON report with `meta` (what was run: the level, the build, the renderer) added
    /// to the bench's own, and returns a one-line summary for the console.
    std::expected<std::string, Error> finish(std::vector<std::pair<std::string, std::string>> meta);

  private:
    /// Spawns the fighters and, with `--bench-god`, makes player 1 invincible; failures are kept for the report.
    void spawn(debug::PlayControls* play);
    /// `--bench-god`: the Entity editor's Invincible on player 1; a failure is kept for the report.
    void makePlayerInvincible(debug::PlayControls* play);

    std::string m_file;
    std::string m_framesFile; // `--bench-frames`: empty for none
    int m_fighters = 0;
    int m_allies = 0;
    bool m_god = false;
    std::uint64_t m_spawnStep = 0;             // `--bench-spawn-step`
    std::uint64_t m_steps = 0;                 // steps of play run so far
    std::optional<std::uint64_t> m_spawnFrame; // the frame of play the fighters spawned on
    std::optional<std::uint64_t> m_measure;    // `--bench-measure`
    bool m_measured = false;                   // that many frames are measured
    std::uint64_t m_warmup = 0;
    std::uint64_t m_frames = 0;
    std::size_t m_spawned = 0;
    std::size_t m_alliesSpawned = 0;
    std::string m_spawnError; // why fewer fighters than asked were spawned
    std::string m_godError;   // why the player could not be made invincible
};

} // namespace coney::platform
