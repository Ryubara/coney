// SPDX-License-Identifier: GPL-3.0-or-later
// The game-facing sound calls over the sound engine and Coney's mixer (docs/research/sound.md).
#include "audio/sound_player.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "core/assert.h"
#include "core/name_hash.h"

namespace coney::audio {

SoundId soundIdOf(std::string_view name) { return crc32(name); }

void SoundPlayer::add(SoundId id, std::shared_ptr<const PcmSound> sound) {
    CONEY_ASSERT(sound != nullptr);
    m_sounds.insert_or_assign(id, std::move(sound));
}

bool SoundPlayer::has(SoundId id) const {
    return m_sounds.contains(id) || (m_engine && m_engine->tables().find(id) != nullptr);
}

// A new player handle for `played`; the zero handle stays invalid.
VoiceHandle SoundPlayer::remember(Played played) {
    const std::uint32_t id = m_nextHandle;
    m_nextHandle = m_nextHandle == std::numeric_limits<std::uint32_t>::max() ? 1 : m_nextHandle + 1;
    m_played.insert_or_assign(id, played);
    return VoiceHandle{id};
}

const SoundPlayer::Played* SoundPlayer::lookup(VoiceHandle sound) const {
    const auto it = m_played.find(sound.id);
    return it == m_played.end() ? nullptr : &it->second;
}

// The engine's play arguments for a caller's voice parameters: the volume, the pitch, and the pan as two gains (a
// balance: Coney's reading of a single pan number).
SoundPlay SoundPlayer::engineParams(const VoiceParams& params) {
    const float pan = std::clamp(params.pan, -1.0F, 1.0F);
    return SoundPlay{.volume = params.volume,
                     .pitch = params.pitch,
                     .panLeft = pan > 0.0F ? 1.0F - pan : 1.0F,
                     .panRight = pan < 0.0F ? 1.0F + pan : 1.0F};
}

VoiceHandle SoundPlayer::play(SoundId id, const VoiceParams& params) {
    if (const auto found = m_sounds.find(id); found != m_sounds.end()) {
        const VoiceHandle voice = m_mixer.play(found->second, params);
        return voice.valid() ? remember(Played{.engine = false, .id = voice.id}) : VoiceHandle{};
    }
    if (m_engine) {
        const SoundHandle sound = m_engine->play(id, engineParams(params));
        if (sound.valid()) {
            return remember(Played{.engine = true, .id = sound.id});
        }
        if (m_engine->tables().find(id) != nullptr) {
            return {}; // a known sound the engine could not play now (refused): not missing
        }
    }
    ++m_missing;
    return {};
}

VoiceHandle SoundPlayer::play3D(SoundId id, SoundVec position, const VoiceParams& params) {
    if (!m_engine || m_sounds.contains(id)) {
        return play(id, params);
    }
    SoundPlay how = engineParams(params);
    how.position = position;
    const SoundHandle sound = m_engine->play(id, how);
    if (!sound.valid()) {
        if (m_engine->tables().find(id) == nullptr) {
            ++m_missing;
        }
        return {};
    }
    return remember(Played{.engine = true, .id = sound.id});
}

void SoundPlayer::stop(VoiceHandle sound, float fadeOutMs) {
    const Played* played = lookup(sound);
    if (played == nullptr) {
        return;
    }
    if (played->engine) {
        if (m_engine) {
            m_engine->stop(SoundHandle{played->id}, fadeOutMs);
        }
    } else {
        m_mixer.stop(VoiceHandle{played->id});
    }
}

void SoundPlayer::setPosition(VoiceHandle sound, SoundVec position) {
    if (const Played* played = lookup(sound); played != nullptr && played->engine && m_engine) {
        m_engine->setPosition(SoundHandle{played->id}, position);
    }
}

void SoundPlayer::setVolume(VoiceHandle sound, float volume) {
    const Played* played = lookup(sound);
    if (played == nullptr) {
        return;
    }
    if (played->engine) {
        if (m_engine) {
            m_engine->setVolume(SoundHandle{played->id}, volume);
        }
    } else {
        m_mixer.setVolume(VoiceHandle{played->id}, volume);
    }
}

void SoundPlayer::pauseAll() {
    if (m_engine) {
        m_engine->pause();
    } else {
        m_mixer.pauseAll();
    }
}

void SoundPlayer::resumeAll() {
    if (m_engine) {
        m_engine->resume();
    } else {
        m_mixer.resumeAll();
    }
}

bool SoundPlayer::isPlaying(VoiceHandle sound) const {
    const Played* played = lookup(sound);
    return played != nullptr && playing(*played);
}

// Whether what a handle stands for still plays.
bool SoundPlayer::playing(const Played& played) const {
    if (played.engine) {
        return m_engine && m_engine->isPlaying(SoundHandle{played.id});
    }
    return m_mixer.isPlaying(VoiceHandle{played.id});
}

void SoundPlayer::setListeners(std::span<const Listener> listeners) {
    m_listeners.assign(listeners.begin(), listeners.end());
}

void SoundPlayer::update(float milliseconds) {
    if (m_engine) {
        m_engine->update(milliseconds, m_listeners);
    }
    // Forget the handles of sounds that have ended.
    std::erase_if(m_played, [this](const auto& entry) { return !playing(entry.second); });
}

} // namespace coney::audio
