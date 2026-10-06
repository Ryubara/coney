// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "audio/sound_player.h"
#include "world_objects/object_services.h"

namespace coney::audio {

/// The glass panes', doors' and barriers' sounds through a SoundPlayer: the ObjectServices sound calls, and nothing
/// else (gameplay forwards these to it). A null player plays nothing, so a run without sound needs no other services.
///
/// **Coney's stand-ins** until the sound list, the sound matrix and the 3D task update are in
/// (docs/research/sound.md#play, docs/research/sound.md#three-d):
/// - A name-hash sound plays at its recorded volume on the effects bus, with no distance attenuation or pan.
/// - A material pair's sound (the sound matrix) and the lock pick's click are counted, not played: Coney has no sound
///   matrix and no interface cue table yet.
///
/// Research: docs/research/objects.md#coneys-implementation
class ObjectSounds final : public world_objects::ObjectServices {
  public:
    /// Sounds through `player` (null: none), which must outlive it or be replaced first.
    explicit ObjectSounds(SoundPlayer* player = nullptr) : m_player(player) {}

    /// Plays through `player` from now on (null: none), as the audio starts after the game modes are made.
    void setPlayer(SoundPlayer* player) { m_player = player; }

    /// Plays the sound whose name hashes to `nameHash` (a door's open or close sound, a hit's).
    void playSound(std::uint32_t nameHash, anim::Vec3 at) override;
    /// Counted: the sound matrix is not in Coney yet.
    void playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 at) override;
    /// Counted: the interface cues are not in Coney yet.
    void lockPickClick(double human) override;

    /// The name-hash sounds asked for.
    [[nodiscard]] std::uint64_t played() const { return m_played; }
    /// The material pairs and clicks asked for, which play nothing yet.
    [[nodiscard]] std::uint64_t unplayed() const { return m_unplayed; }

  private:
    SoundPlayer* m_player;
    std::uint64_t m_played = 0;
    std::uint64_t m_unplayed = 0;
};

} // namespace coney::audio
