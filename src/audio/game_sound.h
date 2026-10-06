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
    void addAmbientSound(int index, std::uint32_t sound) override;
    double addAmbientEmitter(const script::AmbientEmitterCall& call) override;
    void setAmbientEmitterPositions(std::string_view name, std::span<const std::array<float, 3>> positions) override;
    void playAmbientTrack(std::uint32_t sound) override;
    /// SoundEngine::play() as a 2D sound; the engine's handle id.
    double play2D(std::uint32_t sound) override;
    void stopAmbientTrack() override;
    /// SoundPlayer::pauseAll() or resumeAll().
    void pauseSound(bool on) override;
    void setAmbientTrackVolume(float volume) override;
    void playMusic(std::uint32_t track, bool loop, std::string_view callback) override;
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

  private:
    // Where the human with `handle` is, from the AI host's live humans, else where the scripts made him.
    [[nodiscard]] std::optional<SpeakerPlace> locate(double handle) const;
    // The handle of player 1's human, when the scripts made one.
    [[nodiscard]] std::optional<double> playerHandle() const;
    // Puts the listener at player 1's camera (and, for listener 1, at the player).
    void updateListener();
    // Runs the callbacks of the lines that ended.
    void runCallbacks(std::span<const Speech::Ended> ended);
    // Writes a line to the log, if there is one.
    void write(std::string_view line) const;

    SoundPlayer& m_sounds;
    std::function<void(std::string_view)> m_log;
    script::ScriptSystem* m_scripts = nullptr;
    const script::BindingContext* m_context = nullptr;
    AmbientEmitters m_emitters;
    VoiceTable m_voices;
    Speech m_speech;
    int m_listenerMode = 0;
    Listener m_listener{};
};

} // namespace coney::audio
