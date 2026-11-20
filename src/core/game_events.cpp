// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/game_events.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coney {

bool validGameEventKind(std::uint8_t kind) {
    return kind >= static_cast<std::uint8_t>(kFirstGameEventKind) &&
           kind <= static_cast<std::uint8_t>(kLastGameEventKind);
}

std::uint8_t gameEventFields(GameEventKind kind) {
    switch (kind) {
    case GameEventKind::Sound2D:
    case GameEventKind::LoopStart:
    case GameEventKind::SceneStart:
    case GameEventKind::SceneEnd:
        return kEventId;
    case GameEventKind::Sound3D:
        return kEventId | kEventPosition;
    case GameEventKind::HumanSound:
    case GameEventKind::Hit:
        return kEventSubject | kEventId | kEventValue | kEventPosition;
    case GameEventKind::MusicStart:
        return kEventId | kEventValue | kEventExtra;
    case GameEventKind::MusicStop:
    case GameEventKind::LoopStop:
    case GameEventKind::SoundStop:
    case GameEventKind::SoundMove:
        return 0;
    case GameEventKind::ParticleStart:
        return kEventSubject | kEventId | kEventValue | kEventPosition | kEventText | kEventRotation;
    case GameEventKind::ObjectBreak:
        return kEventSubject | kEventId | kEventPosition;
    case GameEventKind::HudMessage:
        return kEventId | kEventValue | kEventText;
    case GameEventKind::Objective:
        return kEventId | kEventValue | kEventExtra | kEventText;
    case GameEventKind::MovieStart:
    case GameEventKind::MovieEnd:
        return kEventValue | kEventText;
    case GameEventKind::Speech:
        return kEventSubject | kEventValue | kEventText;
    case GameEventKind::Command:
        return kEventSubject | kEventId | kEventValue | kEventExtra;
    case GameEventKind::SpeechStop:
        return kEventSubject | kEventValue;
    case GameEventKind::MaterialPair:
        return kEventId | kEventValue | kEventExtra | kEventPosition;
    case GameEventKind::Cue:
        return kEventSubject | kEventId | kEventValue | kEventPosition;
    case GameEventKind::Hint:
        return kEventId | kEventValue | kEventText;
    }
    return 0;
}

bool reliableGameEvent(GameEventKind kind) {
    switch (kind) {
    // One-shots: a lost one is a sound or a puff nobody misses, and one resent late would play out of time.
    case GameEventKind::Sound2D:
    case GameEventKind::Sound3D:
    case GameEventKind::HumanSound:
    case GameEventKind::Hit:
    case GameEventKind::ParticleStart:
    case GameEventKind::ObjectBreak:
    case GameEventKind::MaterialPair:
    case GameEventKind::Cue:
        return false;
    // What lasts or must be read: a lost music change or message would stay wrong until the next one.
    case GameEventKind::MusicStart:
    case GameEventKind::MusicStop:
    case GameEventKind::LoopStart:
    case GameEventKind::LoopStop:
    case GameEventKind::HudMessage:
    case GameEventKind::Objective:
    case GameEventKind::SceneStart:
    case GameEventKind::SceneEnd:
    case GameEventKind::MovieStart:
    case GameEventKind::MovieEnd:
    // A line is long and heard as words: a lost one, or a lost stop, would leave a guest's speaker wrong.
    case GameEventKind::Speech:
    case GameEventKind::Command:
    case GameEventKind::SpeechStop:
    case GameEventKind::Hint:
    case GameEventKind::SoundStop:
    case GameEventKind::SoundMove:
        return true;
    }
    return true;
}

std::string_view describe(GameEventKind kind) {
    switch (kind) {
    case GameEventKind::Sound2D:
        return "sound2d";
    case GameEventKind::Sound3D:
        return "sound3d";
    case GameEventKind::HumanSound:
        return "human_sound";
    case GameEventKind::Hit:
        return "hit";
    case GameEventKind::MusicStart:
        return "music_start";
    case GameEventKind::MusicStop:
        return "music_stop";
    case GameEventKind::LoopStart:
        return "loop_start";
    case GameEventKind::LoopStop:
        return "loop_stop";
    case GameEventKind::ParticleStart:
        return "particle_start";
    case GameEventKind::ObjectBreak:
        return "object_break";
    case GameEventKind::HudMessage:
        return "hud_message";
    case GameEventKind::Objective:
        return "objective";
    case GameEventKind::SceneStart:
        return "scene_start";
    case GameEventKind::SceneEnd:
        return "scene_end";
    case GameEventKind::MovieStart:
        return "movie_start";
    case GameEventKind::MovieEnd:
        return "movie_end";
    case GameEventKind::Speech:
        return "speech";
    case GameEventKind::Command:
        return "command";
    case GameEventKind::SpeechStop:
        return "speech_stop";
    case GameEventKind::MaterialPair:
        return "material_pair";
    case GameEventKind::Cue:
        return "cue";
    case GameEventKind::Hint:
        return "hint";
    case GameEventKind::SoundStop:
    case GameEventKind::SoundMove:
        break;
    }
    return "unknown";
}

std::string describe(const GameEvent& event) {
    std::string line(describe(event.kind));
    const std::uint8_t fields = gameEventFields(event.kind);
    if ((fields & kEventSubject) != 0) {
        line += std::format(" subject={:.0f}", event.subject);
    }
    if ((fields & kEventId) != 0) {
        line += std::format(" id={:#x}", event.id);
    }
    if ((fields & kEventValue) != 0) {
        line += std::format(" value={}", event.value);
    }
    if ((fields & kEventExtra) != 0) {
        line += std::format(" extra={:#x}", event.extra);
    }
    if ((fields & kEventPosition) != 0) {
        line += std::format(" at={:.3f},{:.3f},{:.3f}", event.position[0], event.position[1], event.position[2]);
    }
    if ((fields & kEventRotation) != 0) {
        line += std::format(" rot={:.3f},{:.3f},{:.3f},{:.3f}", event.rotation[0], event.rotation[1], event.rotation[2],
                            event.rotation[3]);
    }
    if ((fields & kEventText) != 0) {
        line += std::format(" text=\"{}\"", event.text);
    }
    return line;
}

void raiseSceneChanges(GameEvents* events, std::vector<std::uint32_t>& before, std::vector<std::uint32_t> now) {
    std::vector<std::uint32_t> ended;
    std::ranges::set_difference(before, now, std::back_inserter(ended));
    std::vector<std::uint32_t> started;
    std::ranges::set_difference(now, before, std::back_inserter(started));
    for (const std::uint32_t id : ended) {
        raiseGameEvent(events, GameEvent{.kind = GameEventKind::SceneEnd, .id = id});
    }
    for (const std::uint32_t id : started) {
        raiseGameEvent(events, GameEvent{.kind = GameEventKind::SceneStart, .id = id});
    }
    before = std::move(now);
}

void GameEvents::raise(GameEvent event) {
    if (!m_recording) {
        return;
    }
    if (event.text.size() > kMaxGameEventText) {
        event.text.resize(kMaxGameEventText);
    }
    m_raised.push_back(std::move(event));
}

std::vector<GameEvent> GameEvents::take() { return std::exchange(m_raised, {}); }

} // namespace coney
