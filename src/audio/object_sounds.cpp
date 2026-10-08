// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/object_sounds.h"

#include "audio/sound_engine.h"

namespace coney::audio {

namespace {

// An object's position as the engine's.
SoundVec soundAt(anim::Vec3 at) { return SoundVec{at.x, at.y, at.z}; }

} // namespace

void ObjectSounds::playSound(std::uint32_t nameHash, anim::Vec3 at) {
    ++m_played;
    if (m_player != nullptr) {
        // The objects' hashes are CRC-32s of the sound names, the ids the SoundPlayer keys its sounds by; a sound whose
        // class has no position plays as 2D.
        (void)m_player->play3D(SoundId{nameHash}, soundAt(at), VoiceParams{.bus = Bus::Sfx});
    }
}

void ObjectSounds::playCueAt(int cue, anim::Vec3 at) {
    const SoundEngine* engine = m_player != nullptr ? m_player->engine() : nullptr;
    const std::uint32_t sound =
        engine != nullptr && cue >= 0 ? engine->interfaceSound(static_cast<std::size_t>(cue)) : 0;
    if (sound != 0) {
        playSound(sound, at);
    }
}

void ObjectSounds::playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 at, float volume) {
    ++m_materialPairs;
    if (m_materials == nullptr) {
        ++m_unplayed;
        return;
    }
    // Sound_PlayMaterialPairAt is this at volume 1; a quieter break passes its own.
    m_materials->playMaterialPair(volume, a, b, soundAt(at));
}

void ObjectSounds::lockPickClick(double /*human*/) { ++m_unplayed; }

} // namespace coney::audio
