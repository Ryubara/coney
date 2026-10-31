// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "net/snapshot.h"

// The client's jitter buffer: the snapshots it has decoded, by the server's step, and a playback clock that runs a
// little behind the newest so the world is drawn between two of them however unevenly they arrive. Coney's own; the
// original has no network play (docs/guides/client-world.md#jitter-buffer).

namespace coney::net {

/// How a JitterBuffer plays. Durations are in the server's fixed steps, never in real time.
/// @coney-only network play; the original has none
struct JitterConfig {
    /// How far behind the newest snapshot the world is drawn: 3 steps, 100 ms at 30 Hz, three snapshots at 30 Hz and
    /// two at 15 Hz, so one lost snapshot still leaves two to blend
    /// (docs/guides/network-architecture.md#interpolation).
    std::uint32_t delaySteps = 3;
    /// How much faster or slower than a step a client step plays while the delay is off its target by more than a
    /// snapshot's interval, percent. 5 catches up a step in 20 (two thirds of a second at 30 Hz), slowly enough not to
    /// be seen. Coney's choice.
    std::uint32_t nudgePercent = 5;
    /// Off the target by more than this many steps (a long stall, a server that skipped ahead), the clock jumps to it
    /// instead of easing there: 15 steps, half a second at 30 Hz. Coney's choice.
    std::uint32_t snapSteps = 15;
};

/// The time a JitterBuffer plays, in thousandths of a server step: fine enough for a nudge of whole percents.
/// @coney-only network play; the original has none
inline constexpr std::uint64_t kTimeUnitsPerStep = 1000;

/// What the playback time shows: the two snapshots either side of it and how far it is from the older to the newer.
/// At a snapshot's own step `from` and `to` are that snapshot and `alpha` is 1, so it is drawn exactly as decoded.
/// @coney-only network play; the original has none
struct JitterSample {
    std::uint32_t fromStep = 0;           ///< the older snapshot's server step
    std::uint32_t toStep = 0;             ///< the newer snapshot's server step, at or after the playback time
    const SnapshotHumans* from = nullptr; ///< the older snapshot's humans; not null
    const SnapshotHumans* to = nullptr;   ///< the newer snapshot's humans; not null
    float alpha = 1.0F;                   ///< 0 at `fromStep`, 1 at `toStep`
};

/// The client's snapshots by the server's step and the clock that plays them, a delay behind the newest. The client
/// pushes every snapshot it decodes and advances the clock once a client step; sample() then gives the two snapshots
/// to draw between.
///
/// The clock eases toward its target (the newest step less the delay) by playing a client step as a little more or
/// less than a server step while it is off by more than the snapshots' interval, jumps there when it is off by more
/// than JitterConfig::snapSteps, and holds at the newest snapshot when none newer has come (starved), never playing
/// past what it has. Step numbers may have gaps (a server that dropped time, snapshots at 15 Hz, a lost one): the
/// clock blends across them. Snapshots the clock has passed are dropped, but for the one it is blending from.
/// Plain data with no real clock: the same pushes and advances give the same samples. Presentation-side: the client's
/// own, never read by a simulation.
/// @coney-only network play; the original has none
class JitterBuffer {
  public:
    /// A buffer that plays as `config` says.
    explicit JitterBuffer(JitterConfig config = {});

    /// A decoded snapshot: the humans it holds at the server's `step`. Ignored, returning false, when it is not newer
    /// than the newest pushed (SnapshotReceiver decodes only newer ones, so that is a caller's mistake).
    bool push(std::uint32_t step, const SnapshotHumans& humans);
    /// One client step: the clock moves on by a server step at the speed its distance from the target asks for (or
    /// starts, the delay behind the newest, at the first advance with a snapshot held). Nothing with none held.
    void advance();
    /// The two snapshots either side of the playback time; nothing before the clock has started.
    [[nodiscard]] std::optional<JitterSample> sample() const;

    /// Whether the clock has started.
    [[nodiscard]] bool started() const { return m_started; }
    /// The playback time, kTimeUnitsPerStep to a server step; 0 before the clock starts.
    [[nodiscard]] std::uint64_t time() const { return m_time; }
    /// The newest snapshot's server step; 0 with none.
    [[nodiscard]] std::uint32_t newestStep() const;
    /// The steps between the last two snapshots pushed, at least 1: the snapshots' interval the clock tolerates.
    [[nodiscard]] std::uint32_t intervalSteps() const { return m_intervalSteps; }
    /// How far the playback time is behind the newest snapshot, in time units; 0 before the clock starts.
    [[nodiscard]] std::uint64_t delay() const;
    /// Whether the clock is on its target, within the snapshots' interval: the world is drawn as far behind as asked.
    [[nodiscard]] bool settled() const;
    /// The speed the last advance() played at, percent, before holding at the newest snapshot: 100 on target; 100 plus
    /// or minus JitterConfig::nudgePercent while easing; between 100 and that while stepping back onto whole steps;
    /// the jump's size, which may be far from 100, when it jumped; 100 at the start.
    [[nodiscard]] std::uint32_t speedPercent() const { return m_speedPercent; }
    /// The advances that found nothing newer to play to and held at the newest snapshot.
    [[nodiscard]] std::uint64_t starvedSteps() const { return m_starvedSteps; }
    /// The times the clock jumped to its target rather than easing there.
    [[nodiscard]] std::uint64_t snaps() const { return m_snaps; }
    /// The snapshots held.
    [[nodiscard]] std::size_t size() const { return m_frames.size(); }
    /// How it plays, as made.
    [[nodiscard]] const JitterConfig& config() const { return m_config; }

  private:
    // The clock's target now: the newest step less the delay.
    [[nodiscard]] std::uint64_t target() const;
    // Drops the snapshots older than the one the playback time blends from.
    void prune();

    JitterConfig m_config;
    std::map<std::uint32_t, SnapshotHumans> m_frames; // by server step
    bool m_started = false;
    bool m_correcting = false; // easing toward the target, until within a nudge of it
    std::uint64_t m_time = 0;
    std::uint32_t m_intervalSteps = 1;
    std::uint32_t m_speedPercent = 100;
    std::uint64_t m_starvedSteps = 0;
    std::uint64_t m_snaps = 0;
};

} // namespace coney::net
