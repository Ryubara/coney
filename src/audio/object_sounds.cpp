// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/object_sounds.h"

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

void ObjectSounds::playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 at) {
    ++m_materialPairs;
    if (m_materials == nullptr) {
        ++m_unplayed;
        return;
    }
    m_materials->playMaterialPairAt(a, b, soundAt(at));
}

void ObjectSounds::lockPickClick(double /*human*/) { ++m_unplayed; }

} // namespace coney::audio
