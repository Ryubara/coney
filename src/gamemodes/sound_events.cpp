// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/sound_events.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "core/game_events.h"
#include "human/human_sounds.h"
#include "scripting/sound_bindings.h"

namespace coney {

// ---- The calls a player hears: raised, then played ----

// @coney-only raises the call as a game event, then plays it (network play)
void SoundEvents::humanSound(const script::HumanSoundCall& call) {
    using Kind = human::HumanSound::Kind;
    if (call.sound.kind == Kind::Impact) {
        raise(GameEvent{.kind = GameEventKind::Hit,
                        .subject = call.human,
                        .id = call.sound.material1,
                        .value = call.sound.material2,
                        .extra = 0,
                        .position = {call.sound.at.x, call.sound.at.y, call.sound.at.z},
                        .text = {}});
    } else {
        const std::uint32_t what = call.sound.kind == Kind::Anim ? 0U : call.sound.kind == Kind::StopHeld ? 1U : 2U;
        raise(GameEvent{.kind = GameEventKind::HumanSound,
                        .subject = call.human,
                        .id = call.sound.kind == Kind::Anim ? call.sound.animSound : 0U,
                        .value = what,
                        .extra = 0,
                        .position = call.position,
                        .text = {}});
    }
    if (m_inner != nullptr) {
        m_inner->humanSound(call);
    }
}

// @coney-only raises the call as a game event, then plays it (network play)
void SoundEvents::playAmbientTrack(std::uint32_t sound) {
    raise(GameEvent{.kind = GameEventKind::LoopStart, .id = sound});
    if (m_inner != nullptr) {
        m_inner->playAmbientTrack(sound);
    }
}

// @coney-only raises the call as a game event, then plays it (network play)
double SoundEvents::play2D(std::uint32_t sound) {
    raise(GameEvent{.kind = GameEventKind::Sound2D, .id = sound});
    return m_inner != nullptr ? m_inner->play2D(sound) : 0.0;
}

// @coney-only raises the call as a game event, then plays it (network play)
double SoundEvents::play3D(std::uint32_t sound, const std::array<float, 3>& position) {
    raise(GameEvent{.kind = GameEventKind::Sound3D, .id = sound, .position = position});
    return m_inner != nullptr ? m_inner->play3D(sound, position) : 0.0;
}

// @coney-only raises the call as a game event, then plays it (network play)
void SoundEvents::stopAmbientTrack() {
    raise(GameEvent{.kind = GameEventKind::LoopStop});
    if (m_inner != nullptr) {
        m_inner->stopAmbientTrack();
    }
}

// @coney-only raises the call as a game event, then plays it (network play)
void SoundEvents::playMusic(std::uint32_t track, bool loop, std::string_view callback) {
    // The end callback is the server's own script's business: a client only plays the track.
    raise(GameEvent{.kind = GameEventKind::MusicStart, .id = track, .value = loop ? 1U : 0U});
    if (m_inner != nullptr) {
        m_inner->playMusic(track, loop, callback);
    }
}

// @coney-only raises the call as a game event, then plays it (network play)
void SoundEvents::playSystemMusic(std::uint32_t track, int fadeBars) {
    raise(GameEvent{.kind = GameEventKind::MusicStart,
                    .id = track,
                    .value = 3U,
                    .extra = static_cast<std::uint32_t>(fadeBars > 0 ? fadeBars : 0)});
    if (m_inner != nullptr) {
        m_inner->playSystemMusic(track, fadeBars);
    }
}

// @coney-only raises the call as a game event, then plays it (network play)
void SoundEvents::stopMusic() {
    raise(GameEvent{.kind = GameEventKind::MusicStop});
    if (m_inner != nullptr) {
        m_inner->stopMusic();
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
bool SoundEvents::soundPlaying(double handle) const { return m_inner != nullptr && m_inner->soundPlaying(handle); }

// @coney-only passes the call on to the game's sound (network play's seam)
bool SoundEvents::humanSpeaking(double human) const { return m_inner != nullptr && m_inner->humanSpeaking(human); }

// ---- Everything else: passed on unchanged ----

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::configureMusicTrack(std::uint32_t track, float barMs, float volume) {
    if (m_inner != nullptr) {
        m_inner->configureMusicTrack(track, barMs, volume);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setInterfaceSound(int cue, std::uint32_t sound) {
    if (m_inner != nullptr) {
        m_inner->setInterfaceSound(cue, sound);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::allocateCharacterVoices(int count) {
    if (m_inner != nullptr) {
        m_inner->allocateCharacterVoices(count);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setCommandSoundPercent(int voiceSet, std::uint32_t command, std::uint32_t percent) {
    if (m_inner != nullptr) {
        m_inner->setCommandSoundPercent(voiceSet, command, percent);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::loadSoundBank(std::string_view name) {
    if (m_inner != nullptr) {
        m_inner->loadSoundBank(name);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setNonDuckableDuck(float factor) {
    if (m_inner != nullptr) {
        m_inner->setNonDuckableDuck(factor);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setPitchFactor(float factor) {
    if (m_inner != nullptr) {
        m_inner->setPitchFactor(factor);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
bool SoundEvents::mayGesture(const script::HumanSoundCall& who) {
    return m_inner != nullptr ? m_inner->mayGesture(who) : true;
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::humanRemoved(double human) {
    if (m_inner != nullptr) {
        m_inner->humanRemoved(human);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
bool SoundEvents::loadSoundMatrix(std::string_view name) {
    return m_inner != nullptr ? m_inner->loadSoundMatrix(name) : true;
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::newMaterialSlots(std::uint32_t m1, std::uint32_t m2, std::uint32_t count, std::uint32_t columns,
                                   const std::array<float, 3>& volumes) {
    if (m_inner != nullptr) {
        m_inner->newMaterialSlots(m1, m2, count, columns, volumes);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::newMaterialSound(std::uint32_t index, std::uint32_t m1, std::uint32_t m2,
                                   const std::array<std::optional<std::uint32_t>, 3>& sounds) {
    if (m_inner != nullptr) {
        m_inner->newMaterialSound(index, m1, m2, sounds);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setMaterialSlotCount(std::uint32_t m1, std::uint32_t m2, std::uint32_t count) {
    if (m_inner != nullptr) {
        m_inner->setMaterialSlotCount(m1, m2, count);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::duplicateSoundMaterials(std::uint32_t a, std::uint32_t b) {
    if (m_inner != nullptr) {
        m_inner->duplicateSoundMaterials(a, b);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::newAnimSlots(std::uint32_t event, std::uint32_t count, std::uint32_t columns,
                               const std::array<float, 3>& volumes) {
    if (m_inner != nullptr) {
        m_inner->newAnimSlots(event, count, columns, volumes);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::newAnimSound(std::uint32_t index, std::uint32_t event,
                               const std::array<std::optional<std::uint32_t>, 3>& sounds) {
    if (m_inner != nullptr) {
        m_inner->newAnimSound(index, event, sounds);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::addAmbientSound(int index, std::uint32_t sound) {
    if (m_inner != nullptr) {
        m_inner->addAmbientSound(index, sound);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
double SoundEvents::addAmbientEmitter(const script::AmbientEmitterCall& call) {
    return m_inner != nullptr ? m_inner->addAmbientEmitter(call) : 0.0;
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setAmbientEmitterPositions(std::string_view name, std::span<const std::array<float, 3>> positions) {
    if (m_inner != nullptr) {
        m_inner->setAmbientEmitterPositions(name, positions);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::enableAmbientEmitter(int emitter, bool on) {
    if (m_inner != nullptr) {
        m_inner->enableAmbientEmitter(emitter, on);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setAmbientEmitterVolume(int emitter, float volume) {
    if (m_inner != nullptr) {
        m_inner->setAmbientEmitterVolume(emitter, volume);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setPlayerCovered(bool covered) {
    if (m_inner != nullptr) {
        m_inner->setPlayerCovered(covered);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setCombatFraming(bool framing) {
    if (m_inner != nullptr) {
        m_inner->setCombatFraming(framing);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::markAmbientEvent() {
    if (m_inner != nullptr) {
        m_inner->markAmbientEvent();
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::stopSound(double handle) {
    if (m_inner != nullptr) {
        m_inner->stopSound(handle);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::moveSound(double handle, const std::array<float, 3>& position, float volume) {
    if (m_inner != nullptr) {
        m_inner->moveSound(handle, position, volume);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::pauseSound(bool on) {
    if (m_inner != nullptr) {
        m_inner->pauseSound(on);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setAmbientTrackVolume(float volume) {
    if (m_inner != nullptr) {
        m_inner->setAmbientTrackVolume(volume);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setMusicVolume(float volume) {
    if (m_inner != nullptr) {
        m_inner->setMusicVolume(volume);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::setListener(int listener) {
    if (m_inner != nullptr) {
        m_inner->setListener(listener);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
bool SoundEvents::speak(const script::SpeechCall& call, std::string_view callback, std::optional<double> callbackArg) {
    return m_inner != nullptr ? m_inner->speak(call, callback, callbackArg) : false;
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::shutUp(double human, bool force) {
    if (m_inner != nullptr) {
        m_inner->shutUp(human, force);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
std::optional<double> SoundEvents::sayCommand(const script::CommandCall& call, std::string_view callback) {
    return m_inner != nullptr ? m_inner->sayCommand(call, callback) : std::nullopt;
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::gameplayEntered() {
    if (m_inner != nullptr) {
        m_inner->gameplayEntered();
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::levelLoadStarted(int levelNumber) {
    if (m_inner != nullptr) {
        m_inner->levelLoadStarted(levelNumber);
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::levelLoaded() {
    if (m_inner != nullptr) {
        m_inner->levelLoaded();
    }
}

// @coney-only passes the call on to the game's sound (network play's seam)
void SoundEvents::gameplayLeft() {
    if (m_inner != nullptr) {
        m_inner->gameplayLeft();
    }
}

void playHudCue(const hud::HudSound& sound, GameEvents* /*events*/, int cue, double /*human*/) { sound.playCue(cue); }

} // namespace coney
