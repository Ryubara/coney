// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "core/game_events.h"
#include "scripting/sound_bindings.h"

namespace coney {

/// The game's sound with its events: every call goes on to `inner` (the game's sound, audio::GameSound) unchanged,
/// and the calls a player hears are raised as game events as well (GameEvents::raise()): a 2D or a positional sound,
/// a human's animation sound, the held sound's stop and rage's start, a hit's impact sound, the music's starts and
/// stop, and the ambient track's start and stop. It sits where the game's scripts and gameplay reach the sound (the
/// binding context's `sound`), so the simulation's every sound passes through this one place
/// (docs/guides/network-architecture.md#checklist, item 8; docs/guides/multiplayer.md#events).
///
/// What a call returns is the inner sound's (a handle, whether a line plays), so the game's behaviour is unchanged
/// whether events are recorded or not. Without an inner sound every call does nothing and returns the defaults.
///
/// @coney-only network play's seam on the game's sound; the original has no network play
class SoundEvents final : public script::SoundHost {
  public:
    /// The game's sound `inner` (null: none) with its events raised on `events` (null: none); both must outlive it.
    SoundEvents(script::SoundHost* inner, GameEvents* events) : m_inner(inner), m_events(events) {}

    /// Sends the calls to `inner` from now on (null: none).
    void setInner(script::SoundHost* inner) { m_inner = inner; }
    /// Raises the events on `events` from now on (null: none).
    void setEvents(GameEvents* events) { m_events = events; }

    void configureMusicTrack(std::uint32_t track, float barMs, float volume) override;
    void setInterfaceSound(int cue, std::uint32_t sound) override;
    void allocateCharacterVoices(int count) override;
    void setCommandSoundPercent(int voiceSet, std::uint32_t command, std::uint32_t percent) override;
    void loadSoundBank(std::string_view name) override;
    void setNonDuckableDuck(float factor) override;
    void setPitchFactor(float factor) override;
    /// Raises a HumanSound (an animation sound, the held sound's stop, rage's start) or a Hit (an impact), then plays
    /// it.
    void humanSound(const script::HumanSoundCall& call) override;
    [[nodiscard]] bool mayGesture(const script::HumanSoundCall& who) override;
    void humanRemoved(double human) override;
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
    /// Raises a LoopStart, then plays it.
    void playAmbientTrack(std::uint32_t sound) override;
    /// Raises a Sound2D, then plays it.
    double play2D(std::uint32_t sound) override;
    /// Raises a Sound3D, then plays it.
    double play3D(std::uint32_t sound, const std::array<float, 3>& position) override;
    void enableAmbientEmitter(int emitter, bool on) override;
    void setAmbientEmitterVolume(int emitter, float volume) override;
    void setPlayerCovered(bool covered) override;
    void setCombatFraming(bool framing) override;
    void markAmbientEvent() override;
    /// Raises a LoopStop, then stops it.
    void stopAmbientTrack() override;
    [[nodiscard]] bool soundPlaying(double handle) const override;
    void stopSound(double handle) override;
    void moveSound(double handle, const std::array<float, 3>& position, float volume) override;
    void pauseSound(bool on) override;
    void setAmbientTrackVolume(float volume) override;
    /// Raises a MusicStart (looping or not), then plays it.
    void playMusic(std::uint32_t track, bool loop, std::string_view callback) override;
    /// Raises a MusicStart (system music, with its fade), then plays it.
    void playSystemMusic(std::uint32_t track, int fadeBars) override;
    /// Raises a MusicStop, then stops it.
    void stopMusic() override;
    void setMusicVolume(float volume) override;
    void setListener(int listener) override;
    bool speak(const script::SpeechCall& call, std::string_view callback, std::optional<double> callbackArg) override;
    void shutUp(double human, bool force) override;
    [[nodiscard]] bool humanSpeaking(double human) const override;
    std::optional<double> sayCommand(const script::CommandCall& call, std::string_view callback) override;
    void gameplayEntered() override;
    void levelLoadStarted(int levelNumber) override;
    void levelLoaded() override;
    void gameplayLeft() override;

  private:
    // Raises `event` when there is somewhere to raise it.
    void raise(GameEvent event) { raiseGameEvent(m_events, std::move(event)); }

    script::SoundHost* m_inner;
    GameEvents* m_events;
};

} // namespace coney
