// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/client_event_player.h"

#include <cstdint>
#include <string_view>

namespace coney {

// @coney-only presents the host's events on a guest (network play)
void ClientEventPlayer::apply(const GameEvent& event) {
    const bool shown = applySound(event) || applyHud(event);
    ++(shown ? m_presented : m_unpresented);
}

// @coney-only presents the host's sound events on a guest (network play)
bool ClientEventPlayer::applySound(const GameEvent& event) {
    if (m_sound == nullptr) {
        return false;
    }
    switch (event.kind) {
    case GameEventKind::Sound2D:
    case GameEventKind::Sound3D:
    case GameEventKind::SoundStop:
    case GameEventKind::SoundMove:
        m_sounds.apply(event);
        return true;
    case GameEventKind::MusicStart:
        // Bit 0 loops the track, bit 1 makes it the system music, which fades in over `extra` bars. The end callback is
        // the host's own script's business.
        if ((event.value & 2U) != 0) {
            m_sound->playSystemMusic(event.id, static_cast<int>(event.extra));
        } else {
            m_sound->playMusic(event.id, (event.value & 1U) != 0, std::string_view{});
        }
        return true;
    case GameEventKind::MusicStop:
        m_sound->stopMusic();
        return true;
    case GameEventKind::LoopStart:
        m_sound->playAmbientTrack(event.id);
        return true;
    case GameEventKind::LoopStop:
        m_sound->stopAmbientTrack();
        return true;
    default:
        return false;
    }
}

// @coney-only presents the host's HUD events on a guest (network play)
bool ClientEventPlayer::applyHud(const GameEvent& event) {
    if (m_hud == nullptr) {
        return false;
    }
    switch (event.kind) {
    case GameEventKind::HudMessage:
        m_hud->setAnnouncement(static_cast<int>(event.id), event.text, event.value != 0);
        return true;
    case GameEventKind::Objective:
        // `value` is the mode shifted left by one, with bit 0 for a silent set.
        m_hud->setObjective(static_cast<int>(event.id), event.text, static_cast<int>(event.value >> 1U),
                            (event.value & 1U) != 0, event.extra);
        return true;
    case GameEventKind::Hint:
        switch (event.id) {
        case kHintQueued:
            m_hud->hints().queue(event.text, static_cast<int>(event.value));
            return true;
        case kHintCleared:
            m_hud->hints().clearShowing();
            return true;
        case kHintFlushed:
            m_hud->hints().flush(static_cast<std::uint8_t>(event.value));
            return true;
        case kHintsSwitched:
            m_hud->setGameTutorialText(event.value != 0);
            return true;
        default:
            return false;
        }
    case GameEventKind::Cue:
        // A cue a world object raises at a place is positional, which the HUD's own sound is not.
        if (event.value != 0) {
            return false;
        }
        m_hud->services().sound.playCue(static_cast<int>(event.id));
        return true;
    default:
        return false;
    }
}

} // namespace coney
