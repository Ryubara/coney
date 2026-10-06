// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/object_sounds.h"

namespace coney::audio {

void ObjectSounds::playSound(std::uint32_t nameHash, anim::Vec3 /*at*/) {
    ++m_played;
    if (m_player != nullptr) {
        // The objects' hashes are CRC-32s of the sound names, the ids the SoundPlayer keys its sounds by.
        (void)m_player->play(SoundId{nameHash}, VoiceParams{.bus = Bus::Sfx});
    }
}

void ObjectSounds::playMaterialPair(std::uint8_t /*a*/, std::uint8_t /*b*/, anim::Vec3 /*at*/) { ++m_unplayed; }

void ObjectSounds::lockPickClick(double /*human*/) { ++m_unplayed; }

} // namespace coney::audio
