// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/pad.h"

namespace coney::net {

/// How an InputBuffer plays a client's steps on the server's. Counted in the game's fixed steps; Coney's choices.
struct InputBufferConfig {
    /// How far behind the newest step that has arrived the buffer plays: a packet up to this many steps late still
    /// arrives in time. 1 at 30 Hz is 33 ms, enough for the jitter of a LAN or a good line.
    std::uint32_t delaySteps = 1;
    /// How far past delaySteps the buffer may fall behind before it skips ahead to delaySteps again. A stall adds a
    /// step of delay each step it lasts; this bounds what is left of it once packets flow again.
    std::uint32_t slackSteps = 3;
    /// No new step for more than this many steps lets the pad go: held buttons released, sticks centred. 15 is half a
    /// second at 30 Hz, so a link that stalls does not leave a stick held until it times out.
    std::uint32_t holdSteps = 15;
};

/// What an InputBuffer has done since its player joined.
struct InputBufferStats {
    std::uint64_t applied = 0; ///< steps played with the pad the client sent for them
    std::uint64_t missed = 0;  ///< steps whose pad never came in time: the step before's pad played again
    std::uint64_t stalled = 0; ///< steps the client sent nothing new for: its last pad held, the delay grew by one
    std::uint64_t skipped = 0; ///< steps dropped to bring the delay back to InputBufferConfig::delaySteps
};

/// One remote player's input commands on the server: the pads their client sent, by the client's step, and the one
/// to play on each of the server's steps. The client's steps play in order, one a server step, a little behind the
/// newest that has arrived, so a packet that comes late or out of order is still played on its own step, and a
/// repeated one is played once. Coney's own; the original has no network play
/// (docs/guides/multiplayer.md#input-commands).
///
/// Each server step: add() every step that arrived, then take() once.
class InputBuffer {
  public:
    /// The most steps it holds: more than any delay it plays at, so a step is never overwritten before it is played.
    static constexpr std::size_t kCapacity = 64;

    /// A buffer playing as `config` says; delaySteps + slackSteps must be below kCapacity / 2.
    explicit InputBuffer(InputBufferConfig config = {});

    /// The client's pad in its step `step`. A step already played or already here is ignored.
    void add(std::uint32_t step, const PadSample& pad);
    /// Plays the next of the client's steps on this server step and returns its pad: unplugged until the first pad
    /// is played; the step before's pad when a step is missing or the client sent nothing new; plugged in with
    /// nothing held once nothing new has come for more than holdSteps.
    PadSample take();

    /// The pad take() last returned.
    [[nodiscard]] const PadSample& current() const { return m_current; }
    /// How many of the client's steps have arrived and wait to be played, from the one the next take() plays to the
    /// newest: delaySteps when all is well, more after a stall, 0 or less while packets are late. 0 before the first
    /// add().
    [[nodiscard]] std::int64_t lead() const;
    /// Whether a step has arrived.
    [[nodiscard]] bool started() const { return m_started; }
    [[nodiscard]] const InputBufferStats& stats() const { return m_stats; }
    [[nodiscard]] const InputBufferConfig& config() const { return m_config; }

  private:
    // One held step: valid when `step` is the step it holds.
    struct Slot {
        std::int64_t step = -1;
        PadSample pad;
    };

    // The slot step `step` (0 or more) lives in.
    [[nodiscard]] Slot& slotOf(std::int64_t step) { return m_slots.at(static_cast<std::size_t>(step) % kCapacity); }

    InputBufferConfig m_config;
    std::array<Slot, kCapacity> m_slots{};
    bool m_started = false;   // a step has arrived
    std::int64_t m_next = 0;  // the client's step the next take() plays; below 0 at first, while the delay fills
    std::int64_t m_newest = -1; // the newest step that has arrived
    std::uint32_t m_quiet = 0;  // server steps since a new step last arrived
    bool m_played = false;      // a pad the client sent has been played
    PadSample m_current;        // unplugged until the first is played
    InputBufferStats m_stats;
};

} // namespace coney::net
