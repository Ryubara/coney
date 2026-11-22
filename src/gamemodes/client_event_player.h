// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "core/game_events.h"
#include "gamemodes/sound_event_player.h"
#include "hud/hud.h"
#include "scripting/sound_bindings.h"

namespace coney {

/// What the client game presents of the host's game events (docs/guides/multiplayer.md#events): each event the host's
/// game raised that a guest can show or play, applied to this machine's sound and HUD. Sounds (starts, stops and moves
/// by the host's instance), the music and the ambient loop go to the sound; the announcements, objectives, hints and
/// 2D interface cues go to the HUD. An event of a kind it cannot present yet (a human's sound or line, a particle, a
/// break, a material pair, a positional cue, a scene or a movie) is counted in unpresented() and nothing else.
///
/// Presentation only: it reads the events and changes this machine's sound and HUD, nothing the host's game reads. The
/// caller hands each event over once, in the host's order (net::EventReceiver does).
/// @coney-only network play's guest side of the host's events; the original has no network play
class ClientEventPlayer {
  public:
    /// Presents on `sound` and `hud`, either of which may be null (an event for a missing one is counted unpresented);
    /// both must outlive their use here, or be replaced with setHud().
    ClientEventPlayer(script::SoundHost* sound, hud::Hud* hud) : m_sounds(sound), m_sound(sound), m_hud(hud) {}

    /// Presents the HUD events on `hud` from now on (null: none), as a new level's HUD takes over.
    void setHud(hud::Hud* hud) { m_hud = hud; }
    /// Presents one of the host's events; whether it was presented is counted.
    void apply(const GameEvent& event);
    /// Stops every sound the host started that still plays here, as the session or the level ends.
    void stopAll() {
        if (m_sound != nullptr) {
            m_sounds.stopAll();
        }
    }

    /// How many events were presented.
    [[nodiscard]] std::uint64_t presented() const { return m_presented; }
    /// How many events were of a kind not presented here (or had no sound or HUD to go to).
    [[nodiscard]] std::uint64_t unpresented() const { return m_unpresented; }

  private:
    // Applies a sound or music event to the sound; false when it is not one of those or there is no sound.
    bool applySound(const GameEvent& event);
    // Applies a message, objective, hint or cue event to the HUD; false when it is not one of those or there is no HUD.
    bool applyHud(const GameEvent& event);

    SoundEventPlayer m_sounds;
    script::SoundHost* m_sound;
    hud::Hud* m_hud;
    std::uint64_t m_presented = 0;
    std::uint64_t m_unpresented = 0;
};

} // namespace coney
