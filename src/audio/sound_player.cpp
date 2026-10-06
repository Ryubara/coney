// SPDX-License-Identifier: GPL-3.0-or-later
// The game-facing sound calls over Coney's mixer (docs/research/sound.md#coneys-implementation).
#include "audio/sound_player.h"

#include <utility>

#include "core/assert.h"
#include "core/name_hash.h"

namespace coney::audio {

SoundId soundIdOf(std::string_view name) { return crc32(name); }

void SoundPlayer::add(SoundId id, std::shared_ptr<const PcmSound> sound) {
    CONEY_ASSERT(sound != nullptr);
    m_sounds.insert_or_assign(id, std::move(sound));
}

VoiceHandle SoundPlayer::play(SoundId id, const VoiceParams& params) {
    const auto found = m_sounds.find(id);
    if (found == m_sounds.end()) {
        ++m_missing;
        return {};
    }
    return m_mixer.play(found->second, params);
}

} // namespace coney::audio
