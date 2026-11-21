// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

#include "core/game_events.h"
#include "scripting/sound_bindings.h"

namespace coney {

/// A guest's side of a playing sound's events: it plays the host's sound starts (Sound2D, Sound3D) on this machine's
/// sound and keeps the handle each gave under the host's instance, so that the host's SoundStop and SoundMove for that
/// instance stop and move this machine's sound. An instance it never started (its start was a lost one-shot, or came
/// before this guest joined) is nothing to stop or move. The other kinds are left alone: playing them is the client
/// game mode's (docs/guides/multiplayer.md#events).
///
/// @coney-only network play's guest side of SoundEvents; the original has no network play
class SoundEventPlayer {
  public:
    /// Plays on `sound` (null: plays nothing), which must outlive it.
    explicit SoundEventPlayer(script::SoundHost* sound) : m_sound(sound) {}

    /// Applies one of the host's events: a sound start plays it, a stop or a move acts on the instance's sound.
    void apply(const GameEvent& event);
    /// Stops every sound kept and lets them go, as the session ends.
    void stopAll();
    /// How many host instances have a sound kept here.
    [[nodiscard]] std::size_t instances() const { return m_handles.size(); }

  private:
    // Keeps `handle` for `instance`; nothing for a handle or instance 0.
    void keep(std::uint32_t instance, double handle);

    script::SoundHost* m_sound;
    std::map<std::uint32_t, double> m_handles; // this machine's handle by the host's instance
};

} // namespace coney
