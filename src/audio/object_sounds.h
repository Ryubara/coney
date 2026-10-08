// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "audio/material_sounds.h"
#include "audio/sound_player.h"
#include "world_objects/object_services.h"

namespace coney::audio {

/// The glass panes', doors' and barriers' sounds through a SoundPlayer: the ObjectServices sound calls, and nothing
/// else (gameplay forwards these to it). A null player plays nothing, so a run without sound needs no other services.
/// A name-hash sound plays at the object's position (a 3D sound of the engine, attenuated and panned by its class); a
/// material pair's plays from the sound matrix (`Sound_PlayMaterialPairAt`, docs/research/sound.md#sound-matrix).
///
/// **Coney's stand-in**: the lock pick's click is counted, not played (the cue it plays is not on the page).
///
/// Research: docs/research/objects.md#coneys-implementation
class ObjectSounds final : public world_objects::ObjectServices {
  public:
    /// Sounds through `player` (null: none), which must outlive it or be replaced first.
    explicit ObjectSounds(SoundPlayer* player = nullptr) : m_player(player) {}

    /// Plays through `player` from now on (null: none), as the audio starts after the game modes are made.
    void setPlayer(SoundPlayer* player) { m_player = player; }
    /// Plays material pairs through `materials` from now on (null: they are counted, not played).
    void setMaterialSounds(MaterialSoundPlayer* materials) { m_materials = materials; }

    /// Plays the sound whose name hashes to `nameHash` (a door's open or close sound, a hit's).
    void playSound(std::uint32_t nameHash, anim::Vec3 at) override;
    /// The pair's sounds from the sound matrix at `at`.
    /// @orig 0x00117280 Sound_PlayMaterialPairAt (unknown)
    void playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 at, float volume = 1.0F) override;
    /// Counted: the click's cue is not on the research page.
    void lockPickClick(double human) override;

    /// The name-hash sounds asked for.
    [[nodiscard]] std::uint64_t played() const { return m_played; }
    /// The material pairs asked for.
    [[nodiscard]] std::uint64_t materialPairs() const { return m_materialPairs; }
    /// The clicks (and the material pairs without a matrix) asked for, which play nothing.
    [[nodiscard]] std::uint64_t unplayed() const { return m_unplayed; }

  private:
    SoundPlayer* m_player;
    MaterialSoundPlayer* m_materials = nullptr;
    std::uint64_t m_materialPairs = 0;
    std::uint64_t m_played = 0;
    std::uint64_t m_unplayed = 0;
};

} // namespace coney::audio
