// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/sound_events.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>

#include "core/game_events.h"
#include "human/human_sounds.h"
#include "scripting/sound_bindings.h"

namespace coney {

namespace {

// How many sounds a side keeps before it lets go of those that no longer play: a sound that ends by itself is never
// stopped, so without this the sounds kept would grow for as long as a session lasts.
constexpr std::size_t kKeptBeforePrune = 64;
// A volume as it travels: thousandths, 0 to 1000.
constexpr float kVolumeScale = 1000.0F;

// `volume` (0-1) in thousandths.
std::uint32_t thousandths(float volume) {
    return static_cast<std::uint32_t>(std::lround(std::clamp(volume, 0.0F, 1.0F) * kVolumeScale));
}

// Lets go of the kept sounds `sound` says no longer play, once there are kKeptBeforePrune of them.
template <typename Key, typename Value, typename HandleOf>
void pruneEnded(std::map<Key, Value>& kept, const script::SoundHost* sound, HandleOf handleOf) {
    if (kept.size() < kKeptBeforePrune) {
        return;
    }
    std::erase_if(kept, [&](const auto& entry) { return sound == nullptr || !sound->soundPlaying(handleOf(entry)); });
}

} // namespace

// ---- A playing sound's instance ----

// @coney-only names a sound's later stop and move for a guest (network play)
std::uint32_t SoundEvents::nextInstance() {
    if (!recording()) {
        // Offline nothing is raised for a stop or move: the sounds kept from a session are let go.
        m_playing.clear();
        return 0;
    }
    ++m_lastInstance;
    if (m_lastInstance == 0) {
        m_lastInstance = 1; // 0 is no instance; after 2^32 sounds the numbers start again
    }
    return m_lastInstance;
}

// @coney-only names a sound's later stop and move for a guest (network play)
void SoundEvents::keep(double handle, std::uint32_t instance, const std::array<float, 3>& position) {
    if (handle == 0.0 || instance == 0) {
        return;
    }
    pruneEnded(m_playing, m_inner, [](const auto& entry) { return entry.first; });
    m_playing[handle] = Playing{.instance = instance, .position = position, .volume = thousandths(1.0F)};
}

// @coney-only raises the stop of a sound started while recording, then stops it (network play)
void SoundEvents::stopSound(double handle) {
    if (!recording()) {
        m_playing.clear();
    } else if (const auto found = m_playing.find(handle); found != m_playing.end()) {
        raise(GameEvent{.kind = GameEventKind::SoundStop, .value = found->second.instance});
        m_playing.erase(found);
    }
    if (m_inner != nullptr) {
        m_inner->stopSound(handle);
    }
}

// @coney-only raises a change of a sound started while recording, then moves it (network play)
void SoundEvents::moveSound(double handle, const std::array<float, 3>& position, float volume) {
    if (!recording()) {
        m_playing.clear();
    } else if (const auto found = m_playing.find(handle); found != m_playing.end()) {
        Playing& playing = found->second;
        const std::uint32_t level = thousandths(volume);
        if (playing.position != position || playing.volume != level) {
            raise(GameEvent{
                .kind = GameEventKind::SoundMove, .value = playing.instance, .extra = level, .position = position});
            playing.position = position;
            playing.volume = level;
        }
    }
    if (m_inner != nullptr) {
        m_inner->moveSound(handle, position, volume);
    }
}

// ---- A guest's side: the host's sounds on this machine ----

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
            m_sound->moveSound(found->second, event.position, static_cast<float>(event.extra) / kVolumeScale);
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
    pruneEnded(m_handles, m_sound, [](const auto& entry) { return entry.second; });
    m_handles[instance] = handle;
}

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

// @coney-only raises the call as a game event with the sound's instance, then plays it (network play)
double SoundEvents::play2D(std::uint32_t sound) {
    const std::uint32_t instance = nextInstance();
    raise(GameEvent{.kind = GameEventKind::Sound2D, .id = sound, .value = instance});
    const double handle = m_inner != nullptr ? m_inner->play2D(sound) : 0.0;
    keep(handle, instance, {});
    return handle;
}

// @coney-only raises the call as a game event with the sound's instance, then plays it (network play)
double SoundEvents::play3D(std::uint32_t sound, const std::array<float, 3>& position) {
    const std::uint32_t instance = nextInstance();
    raise(GameEvent{.kind = GameEventKind::Sound3D, .id = sound, .value = instance, .position = position});
    const double handle = m_inner != nullptr ? m_inner->play3D(sound, position) : 0.0;
    keep(handle, instance, position);
    return handle;
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

// @coney-only plays the line, then raises it as a game event when it plays (network play)
bool SoundEvents::speak(const script::SpeechCall& call, std::string_view callback, std::optional<double> callbackArg) {
    // Raised only when a line plays: one that does not (he is speaking, no such sound) is nothing a player hears. The
    // callback is the server's own script's business.
    const bool plays = m_inner != nullptr && m_inner->speak(call, callback, callbackArg);
    if (plays) {
        raise(GameEvent{.kind = GameEventKind::Speech,
                        .subject = call.human,
                        .value = call.interrupt ? 1U : 0U,
                        .text = call.line});
    }
    return plays;
}

// @coney-only raises the call as a game event, then stops the line (network play)
void SoundEvents::shutUp(double human, bool force) {
    raise(GameEvent{.kind = GameEventKind::SpeechStop, .subject = human, .value = force ? 1U : 0U});
    if (m_inner != nullptr) {
        m_inner->shutUp(human, force);
    }
}

// @coney-only plays the command, then raises it as a game event when it plays (network play)
std::optional<double> SoundEvents::sayCommand(const script::CommandCall& call, std::string_view callback) {
    std::optional<double> line = m_inner != nullptr ? m_inner->sayCommand(call, callback) : std::nullopt;
    if (line.has_value()) {
        // The command and the voice set, not the line: which of the set's lines says it is each machine's sound's
        // choice, made outside the simulation.
        raise(GameEvent{.kind = GameEventKind::Command,
                        .subject = call.human,
                        .id = call.command,
                        .value = static_cast<std::uint32_t>(call.voiceSet >= 0 ? call.voiceSet + 1 : 0),
                        .extra = (call.interrupt ? 1U : 0U) | (call.cutting ? 2U : 0U)});
    }
    return line;
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

void playHudCue(const hud::HudSound& sound, GameEvents* events, int cue, double human) {
    raiseGameEvent(events, GameEvent{.kind = GameEventKind::Cue,
                                     .subject = human,
                                     .id = static_cast<std::uint32_t>(cue >= 0 ? cue : 0),
                                     .value = 0});
    sound.playCue(cue);
}

} // namespace coney
