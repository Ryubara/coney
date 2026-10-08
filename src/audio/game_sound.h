// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "audio/ambient_emitters.h"
#include "audio/human_sound_events.h"
#include "audio/material_sounds.h"
#include "audio/sound_matrix.h"
#include "audio/sound_player.h"
#include "audio/speech.h"
#include "audio/voice_table.h"
#include "gamemodes/front_end_services.h"
#include "scripting/sound_bindings.h"

namespace coney::script {
class ScriptSystem;
struct BindingContext;
} // namespace coney::script

namespace coney::audio {

/// The game's sound as the rest of the game drives it, over the SoundPlayer's engine: the sound bindings and gameplay
/// (script::SoundHost), the front end (FrontEndAudio: the `menu` bank, the cues), and each frame the listener at
/// player 1's camera, the level's ambient emitters and the humans' lines, whose script callbacks it runs when they end.
/// Without an engine (no disc) it plays nothing, and every speech request reports that no line played.
///
/// What it reads of the game is the binding context it is connected to (connect()): player 1's cameras, the AI host's
/// humans (the speakers), the humans the scripts made (player 1), the scenes (speech is off while a cinematic plays).
///
/// Game thread only; deterministic in test mode (its draws are the engine's).
///
/// Research: docs/research/sound.md
class GameSound final : public script::SoundHost, public FrontEndAudio {
  public:
    /// How high above player 1's feet the second listener point is, metres (docs/research/level-loading.md#a-frame-of-
    /// play, step 5).
    static constexpr float kPlayerEarHeight = 1.8F;

    /// The game's sound over `sounds` (which must outlive it); `log` gets a line per bank and music track.
    explicit GameSound(SoundPlayer& sounds, std::function<void(std::string_view)> log = {});

    /// Works from now on with `scripts` (the callbacks' Lua state) and `context` (what it reads of the game); either
    /// may be null. Both must outlive their use here.
    void connect(script::ScriptSystem* scripts, const script::BindingContext* context);
    /// Writes its lines to `log` from now on (empty: none).
    void setLog(std::function<void(std::string_view)> log) { m_log = std::move(log); }

    /// A frame of sound, before the engine's update: the listener, then (unless the sound is paused) the emitters and
    /// the lines, running the callbacks of the lines that ended.
    /// @orig 0x0010f810 AudioManager_Update (unknown)
    void update();

    /// The ambient table and emitters.
    [[nodiscard]] const AmbientEmitters& emitters() const { return m_emitters; }
    /// The voice table.
    [[nodiscard]] const VoiceTable& voices() const { return m_voices; }
    /// The humans' lines.
    [[nodiscard]] const Speech& speech() const { return m_speech; }
    /// One line on the game's sound for the end of a run: the humans' sounds asked for and the matrix's sounds started
    /// (counts only).
    [[nodiscard]] std::string summary() const;
    /// The sound matrix the preloads fill.
    [[nodiscard]] SoundMatrix& matrix() { return m_matrix; }
    [[nodiscard]] const SoundMatrix& matrix() const { return m_matrix; }
    /// The matrix's players (contacts, objects, footsteps, animation sounds), into the engine.
    [[nodiscard]] MaterialSoundPlayer& materialSounds() { return m_materialSounds; }
    /// What `SndSetListener` chose (0 the camera, 1 player 1).
    [[nodiscard]] int listenerMode() const { return m_listenerMode; }
    /// The listener the last update used.
    [[nodiscard]] const Listener& listener() const { return m_listener; }

    // script::SoundHost
    void configureMusicTrack(std::uint32_t track, float barMs, float volume) override;
    void setInterfaceSound(int cue, std::uint32_t sound) override;
    void allocateCharacterVoices(int count) override;
    void setCommandSoundPercent(int voiceSet, std::uint32_t command, std::uint32_t percent) override;
    void loadSoundBank(std::string_view name) override;
    void setNonDuckableDuck(float factor) override;
    void setPitchFactor(float factor) override;
    /// The human's animation or hit sound (HumanSoundEvents), with his type's traits.
    void humanSound(const script::HumanSoundCall& call) override;
    bool loadSoundMatrix(std::string_view name) override;
    void newMaterialSlots(std::uint32_t m1, std::uint32_t m2, std::uint32_t count, std::uint32_t columns,
                          const std::array<float, 3>& volumes) override;
    void newMaterialSound(std::uint32_t index, std::uint32_t m1, std::uint32_t m2,
                          const std::array<std::optional<std::uint32_t>, 3>& sounds) override;
    void setMaterialSlotCount(std::uint32_t m1, std::uint32_t m2, std::uint32_t count) override;
    void duplicateSoundMaterials(std::uint32_t a, std::uint32_t b) override;
    void newAnimSlots(std::uint32_t event, std::uint32_t count, std::uint32_t columns,
                      const std::array<float, 3>& volumes) override;
    void newAnimSound(std::uint32_t index, std::uint32_t event,
                      const std::array<std::optional<std::uint32_t>, 3>& sounds) override;
    void addAmbientSound(int index, std::uint32_t sound) override;
    double addAmbientEmitter(const script::AmbientEmitterCall& call) override;
    void setAmbientEmitterPositions(std::string_view name, std::span<const std::array<float, 3>> positions) override;
    void playAmbientTrack(std::uint32_t sound) override;
    /// SoundEngine::play() as a 2D sound; the engine's handle id.
    double play2D(std::uint32_t sound) override;
    /// A positional sound at full volume and normal pitch.
    /// @orig 0x0010fdd0 PlaySound3D (unknown)
    double play3D(std::uint32_t sound, const std::array<float, 3>& position) override;
    /// AmbientEmitters::setEnabled().
    void enableAmbientEmitter(int emitter, bool on) override;
    void setAmbientEmitterVolume(int emitter, float volume) override;
    void setPlayerCovered(bool covered) override { m_playerCovered = covered; }
    void setCombatFraming(bool framing) override { m_combatFraming = framing; }
    /// @orig 0x0010be40 AmbientManager_MarkEvent (unknown)
    void markAmbientEvent() override;
    /// Whether the `_DAM_` emitters' window is open now (AmbientManager_UpdateTimers, `0x0010be78`).
    [[nodiscard]] bool damageWindowOpen() const;
    /// The angry breathing's level now (0 silent to 1), and whether its loop plays.
    [[nodiscard]] float breathingLevel() const { return m_breathing.level; }
    [[nodiscard]] bool breathingPlays() const { return m_breathing.sound.valid(); }
    void stopAmbientTrack() override;
    [[nodiscard]] bool soundPlaying(double handle) const override;
    void stopSound(double handle) override;
    void moveSound(double handle, const std::array<float, 3>& position, float volume) override;
    /// SoundPlayer::pauseAll() or resumeAll().
    void pauseSound(bool on) override;
    void setAmbientTrackVolume(float volume) override;
    void playMusic(std::uint32_t track, bool loop, std::string_view callback) override;
    void playSystemMusic(std::uint32_t track, int fadeBars) override;
    void stopMusic() override;
    void setMusicVolume(float volume) override;
    void setListener(int listener) override;
    bool speak(const script::SpeechCall& call, std::string_view callback, std::optional<double> callbackArg) override;
    void shutUp(double human, bool force) override;
    std::optional<double> sayCommand(const script::CommandCall& call, std::string_view callback) override;
    void gameplayEntered() override;
    void levelLoadStarted(int levelNumber) override;
    void levelLoaded() override;
    void gameplayLeft() override;

    // FrontEndAudio
    void loadBank(std::string_view bank) override;
    void playMusic(std::string_view track) override;
    void playCue(int cue) override;
    /// The engine's bank (the level's own after its load); empty without an engine.
    [[nodiscard]] std::string loadedBank() const override;

  private:
    // The engine as the matrix's players' sink: nothing plays until the SoundPlayer has an engine.
    class EngineSink final : public SoundSink {
      public:
        explicit EngineSink(SoundPlayer& sounds) : m_sounds(sounds) {}
        SoundHandle play(std::uint32_t hash, const SoundPlay& how) override;
        void stop(SoundHandle sound) override;

      private:
        SoundPlayer& m_sounds;
    };

    // The speech side of the humans' sounds: the lines and commands through Speech and the voice table.
    class Voices final : public HumanVoices {
      public:
        explicit Voices(GameSound& owner) : m_owner(owner) {}
        [[nodiscard]] bool speaking(double human) const override;
        void stopLine(double human) override;
        bool sayLine(const script::HumanSoundCall& who, std::uint32_t hash, float volume, bool cut) override;
        bool sayCommand(const script::HumanSoundCall& who, std::uint32_t command, float volume, bool interrupt,
                        bool duckable) override;
        // **Coney's stand-in** for the brains' reaction-kind slots (not built): a camera within 30 m, and at most two
        // humans saying gesture lines at a time.
        // @orig 0x00291ed0 Ambient_MayGesture (unknown)
        bool mayGesture(const script::HumanSoundCall& who) override;
        [[nodiscard]] bool scenePlaying() const override;

      private:
        GameSound& m_owner;
        std::vector<double> m_gesturing; // who holds a gesture slot while his line plays
    };

    // The traits of a human of `type` from the `CfgChar` records (and `CfgBreathingSound`), and whether `human` may
    // speak.
    [[nodiscard]] HumanTraits traitsOf(int type, double human) const;
    // The voice set of a human of `type`, -1 for none.
    [[nodiscard]] int voiceSetOf(int type) const;
    // A speech command `call` at `volume`, duckable or not; its line's handle. Nothing during a cinematic.
    // @orig 0x002205e0 Human_SayCommand (unknown)
    std::optional<double> sayCommandAt(const script::CommandCall& call, std::string_view callback, float volume,
                                       bool duckable);

    // Where the human with `handle` is, from the AI host's live humans, else where the scripts made him.
    [[nodiscard]] std::optional<SpeakerPlace> locate(double handle) const;
    // The handle of player 1's human, when the scripts made one.
    [[nodiscard]] std::optional<double> playerHandle() const;
    // Puts the listener at player 1's camera (and, for listener 1, at the player).
    void updateListener();
    // Runs the ambient emitters with the game's view (AmbientWorld).
    void updateEmitters(SoundEngine& engine);
    // Fades the angry breathing in while the camera frames a fight and out after.
    void updateBreathing(SoundEngine& engine);
    // Runs the callbacks of the lines that ended.
    void runCallbacks(std::span<const Speech::Ended> ended);
    // Writes a line to the log, if there is one.
    void write(std::string_view line) const;

    SoundPlayer& m_sounds;
    std::function<void(std::string_view)> m_log;
    script::ScriptSystem* m_scripts = nullptr;
    const script::BindingContext* m_context = nullptr;
    EngineSink m_engineSink;
    SoundMatrix m_matrix;
    MaterialSoundPlayer m_materialSounds;
    Voices m_humanVoices{*this};
    std::uint64_t m_animSounds = 0;   // the humans' animation sounds asked for
    std::uint64_t m_impactSounds = 0; // the humans' hit sounds asked for
    HumanSoundEvents m_humanSounds;
    AmbientEmitters m_emitters;
    VoiceTable m_voices;
    Speech m_speech;
    int m_listenerMode = 0;
    bool m_playerCovered = false;           // setPlayerCovered()
    bool m_combatFraming = false;           // setCombatFraming()
    std::optional<double> m_ambientEventMs; // markAmbientEvent()'s stamp on the engine's clock (+0x1b78c)
    // The angry breathing (game state +0x234 its sound, +0x248 its state): its loop and level, and the engine's clock
    // at the last update (none: the next update starts the count).
    struct Breathing {
        SoundHandle sound;
        float level = 0.0F;
        std::optional<double> lastMs;
    };
    Breathing m_breathing;
    Listener m_listener{};
};

} // namespace coney::audio
