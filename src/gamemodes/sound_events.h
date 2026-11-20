// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "core/game_events.h"
#include "hud/hud_audio.h"
#include "scripting/sound_bindings.h"

namespace coney {

/// How many sounds a side keeps (SoundEvents, SoundEventPlayer) before it lets go of those that no longer play: a sound
/// that ends by itself is never stopped, so without this the sounds kept would grow for as long as a session lasts.
inline constexpr std::size_t kSoundsKeptBeforePrune = 64;
/// A volume as a SoundMove carries it: thousandths, 0 to 1000.
inline constexpr float kSoundVolumeScale = 1000.0F;

/// Lets go of the sounds in `kept` that `sound` (null: none) says no longer play, once there are
/// kSoundsKeptBeforePrune of them; `handleOf` gives an entry's handle on `sound`.
/// @coney-only bounds the sounds kept for a guest's later stop and move (network play)
template <typename Key, typename Value, typename HandleOf>
void pruneEndedSounds(std::map<Key, Value>& kept, const script::SoundHost* sound, HandleOf handleOf) {
    if (kept.size() < kSoundsKeptBeforePrune) {
        return;
    }
    std::erase_if(kept, [&](const auto& entry) { return sound == nullptr || !sound->soundPlaying(handleOf(entry)); });
}

/// The game's sound with its events: every call goes on to `inner` (the game's sound, audio::GameSound) unchanged,
/// and the calls a player hears are raised as game events as well (GameEvents::raise()): a 2D or a positional sound,
/// a human's animation sound, the held sound's stop and rage's start, a hit's impact sound, the music's starts and
/// stop, the ambient track's start and stop, a line or command said (when it plays) or stopped, and a playing sound's
/// stop and move (the radios' tracks). It sits where the
/// game's scripts and gameplay reach the sound (the binding context's `sound`). The world objects' sounds pass
/// LevelObjectServices and gameplay's HUD cues playHudCue() instead, each raising its own; a scene's and a movie's
/// sounds travel as their start (docs/guides/network-architecture.md#checklist, item 8;
/// docs/guides/multiplayer.md#events, which lists what raises none).
///
/// While recording, each sound it starts (play2D(), play3D()) is given an instance, numbered from 1, that its start
/// carries; a later stopSound() or moveSound() on the handle that start returned raises a SoundStop or a SoundMove that
/// names it, so a guest finds the sound it started for that instance (SoundEventPlayer). A handle is local to each
/// machine; the instance is what travels.
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
    /// Raises a Sound2D with the sound's instance, then plays it.
    double play2D(std::uint32_t sound) override;
    /// Raises a Sound3D with the sound's instance, then plays it.
    double play3D(std::uint32_t sound, const std::array<float, 3>& position) override;
    void enableAmbientEmitter(int emitter, bool on) override;
    void setAmbientEmitterVolume(int emitter, float volume) override;
    void setPlayerCovered(bool covered) override;
    void setCombatFraming(bool framing) override;
    void markAmbientEvent() override;
    /// Raises a LoopStop, then stops it.
    void stopAmbientTrack() override;
    [[nodiscard]] bool soundPlaying(double handle) const override;
    /// Raises a SoundStop when `handle` is a sound started while recording, then stops it.
    void stopSound(double handle) override;
    /// Raises a SoundMove when `handle` is a sound started while recording and its place or volume (to the thousandth)
    /// changes, then moves it: a radio follows its place every frame, which changes nothing to send.
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
    /// Plays the line, then raises a Speech when it plays.
    bool speak(const script::SpeechCall& call, std::string_view callback, std::optional<double> callbackArg) override;
    /// Raises a SpeechStop, then stops the line.
    void shutUp(double human, bool force) override;
    [[nodiscard]] bool humanSpeaking(double human) const override;
    /// Plays the command, then raises a Command when it plays.
    std::optional<double> sayCommand(const script::CommandCall& call, std::string_view callback) override;
    void gameplayEntered() override;
    void levelLoadStarted(int levelNumber) override;
    void levelLoaded() override;
    void gameplayLeft() override;

  private:
    // A sound started while recording, by its inner handle: its instance and what its last event said of it.
    struct Playing {
        std::uint32_t instance = 0;
        std::array<float, 3> position{};
        std::uint32_t volume = 0; // thousandths
    };

    // Raises `event` when there is somewhere to raise it.
    void raise(GameEvent event) { raiseGameEvent(m_events, std::move(event)); }
    // Whether events are kept now.
    [[nodiscard]] bool recording() const { return m_events != nullptr && m_events->recording(); }
    // The instance for a sound about to start: the next while recording, else 0 (and the sounds kept are let go).
    [[nodiscard]] std::uint32_t nextInstance();
    // Keeps the sound `handle` started as `instance` at `position`, at full volume; nothing for a handle or instance 0.
    void keep(double handle, std::uint32_t instance, const std::array<float, 3>& position);

    script::SoundHost* m_inner;
    GameEvents* m_events;
    std::map<double, Playing> m_playing; // the sounds started while recording, by inner handle
    std::uint32_t m_lastInstance = 0;    // the instance the last sound started was given
};

/// Plays interface cue `cue` through the HUD's `sound`, raising it first on `events` (null: none) as a Cue for
/// `human`'s HUD (0: every player's): the cues gameplay itself plays (a revive's flash, a theft's stages), as the HUD's
/// own cues follow from the state a guest's HUD is given (docs/guides/multiplayer.md#events).
/// @coney-only network play's seam on the HUD's cues; the original has no network play
void playHudCue(const hud::HudSound& sound, GameEvents* events, int cue, double human);

} // namespace coney
