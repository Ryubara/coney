// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/game_sound.h"

#include <cmath>
#include <format>
#include <numbers>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "camera/cameras.h"
#include "characters/character_class.h"
#include "core/name_hash.h"
#include "scripting/ai_bindings.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"

namespace coney::audio {

namespace {

// Mode 1's enter (docs/research/level-loading.md#mode-1, step 1): the pitch, the non-duckable duck and the music's duck
// under scenes back to their defaults.
constexpr float kDefaultPitch = 1.0F;
constexpr float kDefaultNiDuck = 0.2F;
constexpr float kDefaultSceneDuck = 0.75F;

// A game-axes vector as the engine's.
SoundVec soundVec(anim::Vec3 v) { return SoundVec{v.x, v.y, v.z}; }

// The way a human with `headingDegrees` faces (0 faces +y, turning anticlockwise seen from above).
SoundVec facingOf(float headingDegrees) {
    const float radians = headingDegrees * std::numbers::pi_v<float> / 180.0F;
    return SoundVec{-std::sin(radians), std::cos(radians), 0.0F};
}

} // namespace

GameSound::GameSound(SoundPlayer& sounds, std::function<void(std::string_view)> log)
    : m_sounds(sounds), m_log(std::move(log)) {}

void GameSound::connect(script::ScriptSystem* scripts, const script::BindingContext* context) {
    m_scripts = scripts;
    m_context = context;
}

void GameSound::update() {
    updateListener();
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr || engine->paused()) {
        return;
    }
    m_emitters.update(*engine);
    const std::vector<Speech::Ended> ended = m_speech.update(*engine, [this](double human) { return locate(human); });
    runCallbacks(ended);
}

// ---- Configuration ----

void GameSound::configureMusicTrack(std::uint32_t track, float barMs, float volume) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->music().configure(track, barMs, volume);
    }
}

void GameSound::setInterfaceSound(int cue, std::uint32_t sound) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr && cue >= 0) {
        engine->setInterfaceSound(static_cast<std::size_t>(cue), sound);
    }
}

void GameSound::allocateCharacterVoices(int count) {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return;
    }
    const SoundTables& tables = engine->tables();
    m_voices.build(count, [&tables](std::uint32_t hash) { return tables.find(hash) != nullptr; });
}

void GameSound::setCommandSoundPercent(int voiceSet, std::uint32_t command, std::uint32_t percent) {
    // The chance is kept as a byte.
    m_voices.setPercent(voiceSet, command, static_cast<std::uint8_t>(percent));
}

void GameSound::loadSoundBank(std::string_view name) { loadBank(name); }

void GameSound::setNonDuckableDuck(float factor) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->setNonDuckableDuck(factor);
    }
}

void GameSound::setPitchFactor(float factor) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->setPitchFactor(factor);
    }
}

// ---- Ambience ----

void GameSound::addAmbientSound(int index, std::uint32_t sound) { m_emitters.setSound(index, sound); }

double GameSound::addAmbientEmitter(const script::AmbientEmitterCall& call) {
    const AmbientEmitterSetup setup{.name = call.name,
                                    .from = SoundVec{call.from[0], call.from[1], call.from[2]},
                                    .to = SoundVec{call.to[0], call.to[1], call.to[2]},
                                    .slot = call.index,
                                    .sound = call.index == -1 ? crc32(call.sound) : 0,
                                    .count = call.count,
                                    .range = call.range,
                                    .minDelay = call.minDelay,
                                    .maxDelay = call.maxDelay};
    return static_cast<double>(m_emitters.add(setup));
}

void GameSound::setAmbientEmitterPositions(std::string_view name, std::span<const std::array<float, 3>> positions) {
    std::vector<SoundVec> points;
    points.reserve(positions.size());
    for (const std::array<float, 3>& p : positions) {
        points.push_back(SoundVec{p[0], p[1], p[2]});
    }
    m_emitters.setPositions(name, points);
}

void GameSound::playAmbientTrack(std::uint32_t sound) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->playAmbientTrack(sound);
    }
}

double GameSound::play2D(std::uint32_t sound) {
    SoundEngine* engine = m_sounds.engine();
    return engine != nullptr ? static_cast<double>(engine->play(sound).id) : 0.0;
}

void GameSound::stopAmbientTrack() {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->stopAmbientTrack();
    }
}

void GameSound::setAmbientTrackVolume(float volume) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->setAmbientTrackVolume(volume);
    }
}

// ---- Music and the listener ----

void GameSound::playMusic(std::uint32_t track, bool loop, std::string_view callback) {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return;
    }
    const MusicRecord* record = engine->tables().findMusic(track);
    write(std::format("music: {}{}\n", record != nullptr ? record->name : std::format("{:#010x}", track),
                      loop ? " (looping)" : ""));
    // A track played once tells its callback (a Lua function's name) when it ends.
    engine->music().setTrackEndCallback([this](std::string_view name) {
        if (m_scripts != nullptr && m_scripts->exists() && !name.empty()) {
            m_scripts->call(name, {});
        }
    });
    engine->music().play(track, loop, std::string(callback));
}

void GameSound::stopMusic() {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->music().stop();
    }
}

void GameSound::setMusicVolume(float volume) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->music().setVolume(volume);
    }
}

void GameSound::setListener(int listener) { m_listenerMode = listener; }

// ---- Speech ----

bool GameSound::speak(const script::SpeechCall& call, std::string_view callback, std::optional<double> callbackArg) {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return false;
    }
    const std::optional<SpeakerPlace> at = locate(call.human);
    const std::uint32_t hash = crc32(call.line);
    if (!at || engine->tables().find(hash) == nullptr) {
        return false;
    }
    return m_speech.say(*engine, call.human, hash, *at, call.interrupt, std::string(callback), callbackArg).valid();
}

void GameSound::shutUp(double human, bool /*force*/) {
    // Coney's stand-in: the human's speech-allowed byte (+0x194) that an unforced call checks is not modelled, so every
    // call stops the line.
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        m_speech.shutUp(*engine, human);
    }
}

std::optional<double> GameSound::sayCommand(const script::CommandCall& call, std::string_view callback) {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr || call.voiceSet < 0) {
        return std::nullopt;
    }
    const std::optional<SpeakerPlace> at = locate(call.human);
    // A line playing and no interrupt: nothing is said, and the counter does not move.
    if (!at || (!call.interrupt && m_speech.speaking(*engine, call.human))) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> line =
        m_voices.nextLine(call.voiceSet, call.command,
                          [engine](std::int32_t low, std::int32_t high) { return engine->random(low, high); });
    if (!line) {
        return std::nullopt;
    }
    const SoundHandle sound =
        m_speech.say(*engine, call.human, *line, *at, call.interrupt, std::string(callback), call.human);
    if (!sound.valid()) {
        return std::nullopt;
    }
    return static_cast<double>(sound.id);
}

// ---- Gameplay ----

void GameSound::gameplayEntered() {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return;
    }
    // Bank loads wait for the level's loading to end; the level's own bank then replaces `sound`.
    engine->setDeferBankLoads(true);
    engine->setPitchFactor(kDefaultPitch);
    engine->setNonDuckableDuck(kDefaultNiDuck);
    engine->music().setSceneDuck(true, kDefaultSceneDuck);
}

void GameSound::levelLoadStarted(int levelNumber) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->startLoadScreen(characters::isArmiesLevel(levelNumber));
    }
}

void GameSound::levelLoaded() {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return;
    }
    engine->endLoadScreen();
    write(std::format("sound bank: {}\n", engine->bankName()));
    // Coney's stand-in: loading has ended, so SndLoadBank loads at once again (who clears +0x3fa58 is not traced).
    engine->setDeferBankLoads(false);
}

void GameSound::gameplayLeft() {
    SoundEngine* engine = m_sounds.engine();
    m_speech.clear(engine);
    m_emitters.clearEmitters(engine);
    m_listenerMode = 0;
    if (engine == nullptr) {
        return;
    }
    engine->stopAmbientTrack();
    engine->stopSceneSound();
    engine->stopAll();
    engine->music().stop();
}

// ---- The front end ----

void GameSound::loadBank(std::string_view bank) {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return;
    }
    if (auto loaded = engine->loadBank(bank); !loaded) {
        write(std::format("sound bank {}: {}\n", bank, loaded.error().message));
    }
}

void GameSound::playMusic(std::string_view track) { playMusic(crc32(track), true, {}); }

void GameSound::playCue(int cue) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr && cue >= 0) {
        engine->playInterfaceSound(static_cast<std::size_t>(cue));
    }
}

// ---- Helpers ----

std::optional<SpeakerPlace> GameSound::locate(double handle) const {
    if (m_context == nullptr) {
        return std::nullopt;
    }
    std::optional<world_objects::Placement> placement;
    if (m_context->ai != nullptr) {
        placement = m_context->ai->humanPlacement(handle);
    }
    if (!placement && m_context->humans != nullptr) {
        placement = m_context->humans->placement(handle);
    }
    if (!placement) {
        return std::nullopt;
    }
    const std::array<float, 3>& p = placement->position;
    return SpeakerPlace{.position = SoundVec{p[0], p[1], p[2]}, .facing = facingOf(placement->headingDegrees)};
}

std::optional<double> GameSound::playerHandle() const {
    if (m_context == nullptr || m_context->humans == nullptr) {
        return std::nullopt;
    }
    const HumanCreation* player = m_context->humans->player(1);
    return player != nullptr ? std::optional<double>(player->handle) : std::nullopt;
}

// The listener (docs/research/level-loading.md#a-frame-of-play, step 5): each update the game stores, per player, the
// camera's matrix and the player's position raised 1.8 m. Which one `SndSetListener` picks is inferred: 0 the camera,
// 1 the player; the ears lie along the camera's right either way.
void GameSound::updateListener() {
    if (m_context == nullptr || m_context->cameras == nullptr) {
        return;
    }
    const camera::CameraView& view = m_context->cameras->view();
    const anim::Vec3 right = anim::normalise(anim::cross(camera::viewForward(view), camera::viewUp(view)));
    m_listener = Listener{.position = soundVec(view.position), .right = soundVec(right)};
    const std::optional<double> player = playerHandle();
    if (m_listenerMode == 1 && player) {
        if (const std::optional<SpeakerPlace> at = locate(*player); at) {
            m_listener.position = SoundVec{at->position.x, at->position.y, at->position.z + kPlayerEarHeight};
        }
    }
    const std::array<Listener, 1> listeners{m_listener};
    m_sounds.setListeners(listeners);
    // The player's own lines are a player's: they are never ducked.
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->setPlayerOwners(player ? std::vector<std::uint32_t>{static_cast<std::uint32_t>(*player)}
                                       : std::vector<std::uint32_t>{});
    }
}

void GameSound::runCallbacks(std::span<const Speech::Ended> ended) {
    if (m_scripts == nullptr || !m_scripts->exists()) {
        return;
    }
    for (const Speech::Ended& line : ended) {
        if (line.callback.empty()) {
            continue;
        }
        if (line.arg) {
            const std::array<script::Value, 1> args{script::Value(*line.arg)};
            m_scripts->call(line.callback, args);
        } else {
            m_scripts->call(line.callback, {});
        }
    }
}

void GameSound::write(std::string_view line) const {
    if (m_log) {
        m_log(line);
    }
}

} // namespace coney::audio
