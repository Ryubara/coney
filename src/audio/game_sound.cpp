// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/game_sound.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "audio/music_player.h"
#include "camera/camera_view.h"
#include "camera/cameras.h"
#include "characters/character_class.h"
#include "characters/character_types.h"
#include "core/name_hash.h"
#include "human/fighter.h"
#include "scenes/scene_player.h"
#include "scripting/ai_bindings.h"
#include "scripting/human_bindings.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"

namespace coney::audio {

namespace {

// The `_DAM_` emitters' window after an AI event, ms (docs/research/sound.md#ambient).
constexpr double kDamWindowOpensMs = 2000.0;
constexpr double kDamWindowClosesMs = 15000.0;

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
    : m_sounds(sounds), m_log(std::move(log)), m_engineSink(sounds), m_materialSounds(m_matrix, &m_engineSink),
      m_humanSounds(m_materialSounds, m_humanVoices, [this](std::int32_t low, std::int32_t high) {
          SoundEngine* engine = m_sounds.engine();
          return engine != nullptr ? engine->random(low, high) : low;
      }) {}

SoundHandle GameSound::EngineSink::play(std::uint32_t hash, const SoundPlay& how) {
    SoundEngine* engine = m_sounds.engine();
    return engine != nullptr ? engine->play(hash, how) : SoundHandle{};
}

void GameSound::EngineSink::stop(SoundHandle sound) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        engine->stop(sound);
    }
}

void GameSound::connect(script::ScriptSystem* scripts, const script::BindingContext* context) {
    m_scripts = scripts;
    m_context = context;
}

void GameSound::update() {
    updateListener();
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return;
    }
    if (engine->paused()) {
        // The breathing's fades do not count the pause.
        m_breathing.lastMs.reset();
        return;
    }
    updateEmitters(*engine);
    updateBreathing(*engine);
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

// ---- The humans' sounds ----

std::string GameSound::summary() const {
    std::string line =
        std::format("game sound: {} animation sounds and {} hit sounds asked for, {} matrix sounds started; {} "
                    "ambient emitters, {} plays; {} lines said",
                    m_animSounds, m_impactSounds, m_materialSounds.started(), m_emitters.emitterCount(),
                    m_emitters.plays(), m_speech.said());
    // What the engine made of the plays: a sound with no record or no sample in the loaded bank stays silent.
    if (const SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        const SoundEngineStats stats = engine->stats();
        line += std::format("; engine: {} started, {} unknown, {} refused, {} without a sample", stats.started,
                            stats.unknown, stats.refused, stats.missingSamples);
    }
    return line + "\n";
}

void GameSound::humanSound(const script::HumanSoundCall& call) {
    ++(call.sound.kind == human::HumanSound::Kind::Anim ? m_animSounds : m_impactSounds);
    if (m_sounds.engine() == nullptr) {
        return;
    }
    m_humanSounds.play(call, traitsOf(call.characterType, call.human));
}

HumanTraits GameSound::traitsOf(int type, double human) const {
    HumanTraits traits;
    if (m_context == nullptr) {
        return traits;
    }
    if (m_context->recorded != nullptr) {
        const script::RecordedCalls& recorded = *m_context->recorded;
        // The type's last CfgChar record: its women's voices and its class.
        const std::span<const std::vector<script::Value>> chars = recorded.calls("CfgChar");
        for (auto call = chars.rbegin(); call != chars.rend(); ++call) {
            const std::optional<characters::CharacterType> parsed = characters::parseCfgChar(*call);
            if (parsed && parsed->type == type) {
                traits.female = parsed->female.value_or(0) == 1;
                traits.bossClass = parsed->category.value_or(0) == human::kBossCategory;
                break;
            }
        }
        // CfgBreathingSound's first argument is the combat factor (game state +0x24c).
        if (const std::span<const std::vector<script::Value>> breathing = recorded.calls("CfgBreathingSound");
            !breathing.empty() && !breathing.back().empty()) {
            if (const std::optional<double> factor = breathing.back().front().number(); factor) {
                traits.combatFactor = static_cast<float>(*factor);
            }
        }
    }
    // HuEnableSoundCommands(false) silences him.
    if (m_context->ai != nullptr) {
        if (script::HumanBindingHost* humans = m_context->ai->humans(); humans != nullptr) {
            const std::optional<script::HumanStatus> status = humans->status(human);
            traits.canSpeak = !status || status->soundCommands;
        }
    }
    return traits;
}

int GameSound::voiceSetOf(int type) const {
    return m_context != nullptr && m_context->recorded != nullptr ? script::voiceSetOfType(*m_context->recorded, type)
                                                                  : -1;
}

bool GameSound::Voices::speaking(double human) const {
    const SoundEngine* engine = m_owner.m_sounds.engine();
    return engine != nullptr && m_owner.m_speech.speaking(*engine, human);
}

void GameSound::Voices::stopLine(double human) {
    if (SoundEngine* engine = m_owner.m_sounds.engine(); engine != nullptr) {
        m_owner.m_speech.shutUp(*engine, human);
    }
}

bool GameSound::Voices::sayLine(const script::HumanSoundCall& who, std::uint32_t hash, float volume, bool /*cut*/) {
    SoundEngine* engine = m_owner.m_sounds.engine();
    if (engine == nullptr || scenePlaying()) {
        return false;
    }
    // A cutting line (HuSpeakNI's kind) is not modelled apart (the human's +0x194): every line may be cut.
    const SpeakerPlace at{.position = SoundVec{who.position[0], who.position[1], who.position[2]},
                          .facing = facingOf(who.headingDegrees)};
    return m_owner.m_speech.say(*engine, who.human, hash, at, true, {}, std::nullopt, volume).valid();
}

bool GameSound::Voices::sayCommand(const script::HumanSoundCall& who, std::uint32_t command, float volume,
                                   bool interrupt, bool duckable) {
    if (!m_owner.traitsOf(who.characterType, who.human).canSpeak) {
        return false;
    }
    const script::CommandCall call{.human = who.human,
                                   .voiceSet = m_owner.voiceSetOf(who.characterType),
                                   .command = command,
                                   .interrupt = interrupt,
                                   .target = 0.0};
    return m_owner.sayCommandAt(call, {}, volume, duckable).has_value();
}

bool GameSound::Voices::mayGesture(const script::HumanSoundCall& who) {
    constexpr float kGestureRange = 30.0F;
    constexpr std::size_t kGestureSlots = 2;
    // The slots of those who no longer speak free up.
    std::erase_if(m_gesturing, [this](double human) { return !speaking(human); });
    if (m_owner.m_context == nullptr || m_owner.m_context->cameras == nullptr) {
        return false;
    }
    const anim::Vec3 camera = m_owner.m_context->cameras->view().position;
    const float dx = camera.x - who.position[0];
    const float dy = camera.y - who.position[1];
    const float dz = camera.z - who.position[2];
    const bool holds = std::ranges::find(m_gesturing, who.human) != m_gesturing.end();
    if ((dx * dx) + (dy * dy) + (dz * dz) > kGestureRange * kGestureRange ||
        (!holds && m_gesturing.size() >= kGestureSlots)) {
        return false;
    }
    if (!holds) {
        m_gesturing.push_back(who.human);
    }
    return true;
}

bool GameSound::Voices::scenePlaying() const {
    const script::BindingContext* context = m_owner.m_context;
    return context != nullptr && context->scenes != nullptr && context->scenes->cinematicActive();
}

// ---- The sound matrix ----

bool GameSound::loadSoundMatrix(std::string_view name) {
    const bool changed = m_matrix.load(name);
    if (changed) {
        write(std::format("sound matrix {}\n", name));
    }
    return changed;
}

void GameSound::newMaterialSlots(std::uint32_t m1, std::uint32_t m2, std::uint32_t count, std::uint32_t columns,
                                 const std::array<float, 3>& volumes) {
    m_matrix.newMaterialSlots(m1, m2, count, columns, volumes);
}

void GameSound::newMaterialSound(std::uint32_t index, std::uint32_t m1, std::uint32_t m2,
                                 const std::array<std::optional<std::uint32_t>, 3>& sounds) {
    m_matrix.newMaterialSound(index, m1, m2, sounds);
}

void GameSound::setMaterialSlotCount(std::uint32_t m1, std::uint32_t m2, std::uint32_t count) {
    m_matrix.setMaterialSlotCount(m1, m2, count);
}

void GameSound::duplicateSoundMaterials(std::uint32_t a, std::uint32_t b) { m_matrix.duplicateMaterials(a, b); }

void GameSound::newAnimSlots(std::uint32_t event, std::uint32_t count, std::uint32_t columns,
                             const std::array<float, 3>& volumes) {
    m_matrix.newAnimSlots(event, count, columns, volumes);
}

void GameSound::newAnimSound(std::uint32_t index, std::uint32_t event,
                             const std::array<std::optional<std::uint32_t>, 3>& sounds) {
    m_matrix.newAnimSound(index, event, sounds);
}

// ---- Ambience ----

void GameSound::addAmbientSound(int index, std::uint32_t sound) { m_emitters.setSound(index, sound); }

double GameSound::addAmbientEmitter(const script::AmbientEmitterCall& call) {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return 0.0;
    }
    const AmbientEmitterSetup setup{.name = call.name,
                                    .from = SoundVec{call.from[0], call.from[1], call.from[2]},
                                    .to = SoundVec{call.to[0], call.to[1], call.to[2]},
                                    .slot = call.index,
                                    .sound = call.index != -1     ? 0
                                             : call.sound.empty() ? call.soundHash
                                                                  : crc32(call.sound),
                                    .count = call.count,
                                    .range = call.range,
                                    .plays = call.plays,
                                    .minDelay = call.minDelay,
                                    .maxDelay = call.maxDelay,
                                    .mode = call.mode,
                                    .filter = call.filter};
    return static_cast<double>(m_emitters.add(setup, *engine));
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

double GameSound::play3D(std::uint32_t sound, const std::array<float, 3>& position) {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return 0.0;
    }
    SoundPlay how;
    how.position = SoundVec{position[0], position[1], position[2]};
    return static_cast<double>(engine->play(sound, how).id);
}

bool GameSound::soundPlaying(double handle) const {
    const SoundEngine* engine = m_sounds.engine();
    return engine != nullptr && handle != 0.0 && engine->isPlaying(SoundHandle{static_cast<std::uint32_t>(handle)});
}

void GameSound::stopSound(double handle) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr && handle != 0.0) {
        engine->stop(SoundHandle{static_cast<std::uint32_t>(handle)});
    }
}

void GameSound::moveSound(double handle, const std::array<float, 3>& position, float volume) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr && handle != 0.0) {
        const SoundHandle sound{static_cast<std::uint32_t>(handle)};
        engine->setPosition(sound, SoundVec{position[0], position[1], position[2]});
        engine->setVolume(sound, volume);
    }
}

void GameSound::enableAmbientEmitter(int emitter, bool on) { m_emitters.setEnabled(emitter, on); }

void GameSound::setAmbientEmitterVolume(int emitter, float volume) {
    if (SoundEngine* engine = m_sounds.engine(); engine != nullptr) {
        m_emitters.setVolume(emitter, volume, *engine);
    }
}

void GameSound::pauseSound(bool on) {
    if (on) {
        m_sounds.pauseAll();
    } else {
        m_sounds.resumeAll();
    }
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

void GameSound::playSystemMusic(std::uint32_t track, int fadeBars) {
    SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return;
    }
    const MusicRecord* record = engine->tables().findMusic(track);
    write(std::format("music: {} (system, fade {} bars)\n",
                      record != nullptr ? record->name : std::format("{:#010x}", track), fadeBars));
    engine->music().play(track, true, {}, fadeBars);
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
    return sayCommandAt(call, callback, 1.0F, true);
}

std::optional<double> GameSound::sayCommandAt(const script::CommandCall& call, std::string_view callback, float volume,
                                              bool duckable) {
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
    const SoundHandle sound = m_speech.say(*engine, call.human, *line, *at, call.interrupt, std::string(callback),
                                           call.human, volume, duckable);
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
    write(std::format("sound matrix {}: {} material pairs, {} animation sounds\n", m_matrix.name(),
                      m_matrix.materialEntries(), m_matrix.animEntries()));
    // Coney's stand-in: loading has ended, so SndLoadBank loads at once again (who clears +0x3fa58 is not traced).
    engine->setDeferBankLoads(false);
}

void GameSound::gameplayLeft() {
    SoundEngine* engine = m_sounds.engine();
    m_speech.clear(engine);
    m_emitters.clearEmitters(engine);
    m_listenerMode = 0;
    // The breathing goes with the other sounds (stopAll below) and starts afresh in the next level, as does the
    // ambient event stamp.
    m_combatFraming = false;
    m_breathing = Breathing{};
    m_ambientEventMs.reset();
    if (engine == nullptr) {
        return;
    }
    engine->stopAmbientTrack();
    engine->stopSceneSound();
    engine->setCinematic(false); // a level left mid-scene leaves no duck behind
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

std::string GameSound::loadedBank() const {
    const SoundEngine* engine = std::as_const(m_sounds).engine();
    return engine != nullptr ? engine->bankName() : std::string();
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
// The angry breathing (docs/research/sound.md#warriors-functions): `CfgBreathingSound(factor, inMs, outMs, sound)`'s
// sound, a 2D loop. While the camera frames player 1's fight it starts at volume 0 and rises to 1 over `inMs`; once it
// does not, it falls over `outMs` and stops at 0; framing again while it falls turns it back up from where it is.
// **Coney's choice**: the level moves linearly with the engine's clock.
// @orig 0x00419150 Breathing_Update (unknown)
// @orig 0x00419030 Breathing_Start (unknown)
// @orig 0x00419108 Breathing_Stop (unknown)
void GameSound::updateBreathing(SoundEngine& engine) {
    const double now = engine.now();
    const double elapsed = m_breathing.lastMs ? now - *m_breathing.lastMs : 0.0;
    m_breathing.lastMs = now;
    if (m_context == nullptr || m_context->recorded == nullptr) {
        return;
    }
    const std::span<const std::vector<script::Value>> calls = m_context->recorded->calls("CfgBreathingSound");
    if (calls.empty() || calls.back().size() < 4) {
        return;
    }
    const std::vector<script::Value>& call = calls.back();
    const std::optional<std::string_view> name = call[3].string();
    if (!name) {
        return;
    }
    const float inMs = static_cast<float>(call[1].number().value_or(0.0));
    const float outMs = static_cast<float>(call[2].number().value_or(0.0));
    if (m_combatFraming) {
        if (!m_breathing.sound.valid()) {
            m_breathing.level = 0.0F;
            m_breathing.sound = engine.play(crc32(*name), SoundPlay{.volume = 0.0F});
        }
        m_breathing.level = inMs > 0.0F ? std::min(1.0F, m_breathing.level + static_cast<float>(elapsed) / inMs) : 1.0F;
    } else if (m_breathing.sound.valid()) {
        m_breathing.level =
            outMs > 0.0F ? std::max(0.0F, m_breathing.level - static_cast<float>(elapsed) / outMs) : 0.0F;
        if (m_breathing.level <= 0.0F) {
            engine.stop(std::exchange(m_breathing.sound, SoundHandle{}));
            return;
        }
    }
    if (m_breathing.sound.valid()) {
        engine.setVolume(m_breathing.sound, m_breathing.level);
    }
}

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

// The emitters' view of the game (docs/research/sound.md#ambient): player 1 and the listener; the scene, the music's
// duck and the fight timer. **Coney's stand-ins**: the fight timer is on while the music's mood is the fight (the
// original's 5 s after it ends are not kept); no AI event opens the `_DAM_` window yet.
// The stamp is the clock, or 2 s before it once a stamp is set, so a noise while one counts opens the window at once.
// **Coney's reading** of "already set": any earlier stamp (the field is never cleared but by a reset).
void GameSound::markAmbientEvent() {
    const SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr) {
        return;
    }
    m_ambientEventMs = m_ambientEventMs ? engine->now() - kDamWindowOpensMs : engine->now();
}

// From 2 s to 15 s after the stamp.
// @orig 0x0010be78 AmbientManager_UpdateTimers (unknown)
bool GameSound::damageWindowOpen() const {
    const SoundEngine* engine = m_sounds.engine();
    if (engine == nullptr || !m_ambientEventMs) {
        return false;
    }
    const double since = engine->now() - *m_ambientEventMs;
    return since >= kDamWindowOpensMs && since <= kDamWindowClosesMs;
}

void GameSound::updateEmitters(SoundEngine& engine) {
    const int mood = m_context != nullptr && m_context->state != nullptr ? m_context->state->story.musicMood : -1;
    const MusicPlayer& music = engine.music();
    const bool musicBusy =
        music.state(0) != MusicState::Idle || music.state(1) != MusicState::Idle || music.state(2) != MusicState::Idle;
    const std::array<SoundVec, 1> listeners{m_listener.position};
    const std::array<bool, 1> covered{m_playerCovered};
    const bool hasPlayer = playerHandle().has_value();
    m_emitters.update(
        engine, AmbientWorld{.listeners = listeners,
                             .playersCovered = hasPlayer ? std::span<const bool>(covered) : std::span<const bool>(),
                             .scenePlaying = engine.cinematic(),
                             .musicDuck = musicBusy && mood != 2,
                             .damWindow = damageWindowOpen(),
                             .fightTimer = mood == 1});
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
