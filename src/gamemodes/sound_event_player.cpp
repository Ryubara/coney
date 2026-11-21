// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/sound_event_player.h"

#include <cstdint>
#include <map>

#include "core/game_events.h"
#include "gamemodes/sound_events.h"
#include "scripting/sound_bindings.h"

namespace coney {

// @coney-only plays the host's sound events on a guest (network play)
void SoundEventPlayer::apply(const GameEvent& event) {
    switch (event.kind) {
    case GameEventKind::Sound2D:
        if (m_sound != nullptr) {
            keep(event.value, m_sound->play2D(event.id));
        }
        break;
    case GameEventKind::Sound3D:
        if (m_sound != nullptr) {
            keep(event.value, m_sound->play3D(event.id, event.position));
        }
        break;
    case GameEventKind::SoundStop:
        if (const auto found = m_handles.find(event.value); found != m_handles.end()) {
            m_sound->stopSound(found->second);
            m_handles.erase(found);
        }
        break;
    case GameEventKind::SoundMove:
        if (const auto found = m_handles.find(event.value); found != m_handles.end()) {
            m_sound->moveSound(found->second, event.position, static_cast<float>(event.extra) / kSoundVolumeScale);
        }
        break;
    default:
        break;
    }
}

// @coney-only plays the host's sound events on a guest (network play)
void SoundEventPlayer::stopAll() {
    for (const auto& [instance, handle] : m_handles) {
        m_sound->stopSound(handle);
    }
    m_handles.clear();
}

// @coney-only plays the host's sound events on a guest (network play)
void SoundEventPlayer::keep(std::uint32_t instance, double handle) {
    if (instance == 0 || handle == 0.0) {
        return;
    }
    pruneEndedSounds(m_handles, m_sound, [](const auto& entry) { return entry.second; });
    m_handles[instance] = handle;
}

} // namespace coney
