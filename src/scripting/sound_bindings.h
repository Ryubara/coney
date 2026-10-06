// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"

namespace coney::script {

/// `AddAmbientSoundEmitter2`'s arguments, as the binding reads them
/// (docs/references/bindings/sound.md#addambientsoundemitter2).
struct AmbientEmitterCall {
    std::string name;            ///< The emitter's name; a name already used refers to the existing emitter.
    std::array<float, 3> from{}; ///< The first point.
    std::array<float, 3> to{};   ///< The second point (the same as `from` for a point source).
    int index = -1;              ///< The first ambient-table entry, or -1 to play `sound`.
    std::string sound;           ///< The sound name when `index` is -1.
    std::uint32_t count = 0;     ///< How many table entries from `index` it picks from.
    float range = -1.0F;         ///< Stored; inferred to be the audible range in metres (-1 the default).
    int arg8 = -1;               ///< Stored; meaning not traced.
    std::uint32_t minDelay = 0;  ///< The shortest pause between plays, seconds (inferred).
    std::uint32_t maxDelay = 0;  ///< The longest pause, seconds (inferred).
    std::uint8_t arg11 = 0;      ///< Stored as a byte; meaning not traced.
    std::uint8_t mode = 0;       ///< 0-2 (larger values become 0); meaning not traced.
};

/// A line a human is to say by name (`HuSpeak`, `HuSpeakNI`).
struct SpeechCall {
    double human = 0.0;     ///< The speaker's handle.
    std::string line;       ///< The sound name (`vags/speeches/l99/...`).
    bool interrupt = false; ///< `HuSpeakNI`: cut off the line the human is saying; `HuSpeak` gives up instead.
    double lookAt = 0.0;    ///< A human the speaker looks at for the line (NilHandle 0 or 0xffffffff: none).
};

/// A speech command a human is to say (`SoundPlayCommand`, docs/research/sound.md#speech).
struct CommandCall {
    double human = 0.0;        ///< The speaker's handle.
    int voiceSet = -1;         ///< The speaker's voice set (its type's `CfgChar` voice), -1 when unknown.
    std::uint32_t command = 0; ///< The speech command id, 0-206.
    bool interrupt = true;     ///< Cut off a line the human is saying; false: say nothing while one plays.
    double target = 0.0;       ///< A human the speaker looks at for the line.
};

/// What the sound bindings and gameplay ask of the game's sound: configuration, the ambience, the music, the listener,
/// the humans' speech and the level's loading. The audio implements it (audio/game_sound.h); a context without one
/// (a test, `--no-audio`) plays nothing, and a speech binding then runs its callback at once, as when no line plays.
///
/// Research: docs/research/sound.md
class SoundHost {
  public:
    virtual ~SoundHost() = default;
    SoundHost() = default;
    SoundHost(const SoundHost&) = delete;
    SoundHost& operator=(const SoundHost&) = delete;
    SoundHost(SoundHost&&) = delete;
    SoundHost& operator=(SoundHost&&) = delete;

    // ---- Configuration (the preloads) ----

    /// `SndCfgMusicInfo(track, bar, volume)`: a music track's bar length (ms) and volume.
    virtual void configureMusicTrack(std::uint32_t track, float barMs, float volume) = 0;
    /// `SoundCfgInterfaceSound(cue, sound)`.
    virtual void setInterfaceSound(int cue, std::uint32_t sound) = 0;
    /// `SndAllocateCharacterVoices(count)`: the voice table of `count` voice sets.
    virtual void allocateCharacterVoices(int count) = 0;
    /// `SndSetCommandSoundPercent(set, command, percent)`; set -1 for every set.
    virtual void setCommandSoundPercent(int voiceSet, std::uint32_t command, std::uint32_t percent) = 0;
    /// `SndLoadBank(name)`: loads a bank now, or records it as the pending one while loading is deferred.
    virtual void loadSoundBank(std::string_view name) = 0;
    /// `SndSetNIDuck(level)`: what directional sounds duck to under a non-duckable one.
    virtual void setNonDuckableDuck(float factor) = 0;
    /// `SndSetPitchMod(pitch)`: the global pitch factor.
    virtual void setPitchFactor(float factor) = 0;

    // ---- Ambience ----

    /// `AddAmbientSound(index, sound)`: an entry of the ambient table.
    virtual void addAmbientSound(int index, std::uint32_t sound) = 0;
    /// `AddAmbientSoundEmitter2`: the emitter's id.
    virtual double addAmbientEmitter(const AmbientEmitterCall& call) = 0;
    /// `SetAmbientEmitterPositions(name, ...)`: the named emitter's positions (at most five).
    virtual void setAmbientEmitterPositions(std::string_view name, std::span<const std::array<float, 3>> positions) = 0;
    /// `SoundPlayAmbientTrack(name)`.
    virtual void playAmbientTrack(std::uint32_t sound) = 0;
    /// `SoundStopAmbientTrack()`.
    virtual void stopAmbientTrack() = 0;
    /// `SetAmbientTrackVolume(volume)`, 0-1.
    virtual void setAmbientTrackVolume(float volume) = 0;

    // ---- Music and the listener ----

    /// `SoundPlayMusicTrack` (`loop` false, with the end `callback`) and `SoundLoopMusicTrack` (`loop` true).
    virtual void playMusic(std::uint32_t track, bool loop, std::string_view callback) = 0;
    /// `SoundStopMusicTrack()`.
    virtual void stopMusic() = 0;
    /// `SoundSetMusicVolume(volume)`.
    virtual void setMusicVolume(float volume) = 0;
    /// `SndSetListener(listener)`: what 3D sound is heard from (0 the camera, 1 the player; inferred).
    virtual void setListener(int listener) = 0;

    // ---- Speech ----

    /// Makes a human say a line by name, its callback to run when the line ends. False when no line plays (no such
    /// human or sound, or the human is speaking and the call does not interrupt): the caller then runs the callback.
    virtual bool speak(const SpeechCall& call, std::string_view callback, std::optional<double> callbackArg) = 0;
    /// `HuShutUp(human, force)`: stops the line the human is saying.
    virtual void shutUp(double human, bool force) = 0;
    /// Makes a human say a speech command; the callback is called with the speaker's handle when the line ends. The
    /// line's handle, or nothing when no line plays (the caller then runs the callback).
    virtual std::optional<double> sayCommand(const CommandCall& call, std::string_view callback) = 0;

    // ---- Gameplay (mode 1) ----

    /// Mode 1's enter, before InitLevel (docs/research/level-loading.md#mode-1, step 1).
    virtual void gameplayEntered() = 0;
    /// InitLevel step 3: the load screen's bank and sounds for the level numbered `levelNumber`.
    virtual void levelLoadStarted(int levelNumber) = 0;
    /// InitLevel step 11: the load-screen sounds stop and the level's bank loads.
    virtual void levelLoaded() = 0;
    /// Mode 1's exit: the sounds and music stop; the level's emitters and lines go.
    virtual void gameplayLeft() = 0;
};

/// The sound bindings: the configuration the preloads make, the ambience, the music, the listener, the speech lines by
/// name and the speech commands. All real; installBindings() registers them.
inline constexpr std::array<std::string_view, 22> kSoundBindings{"AddAmbientSound",
                                                                 "AddAmbientSoundEmitter2",
                                                                 "HuShutUp",
                                                                 "HuSpeak",
                                                                 "HuSpeakNI",
                                                                 "SetAmbientEmitterPositions",
                                                                 "SetAmbientTrackVolume",
                                                                 "SndAllocateCharacterVoices",
                                                                 "SndCfgMusicInfo",
                                                                 "SndLoadBank",
                                                                 "SndSetCommandSoundPercent",
                                                                 "SndSetListener",
                                                                 "SoundCfgInterfaceSound",
                                                                 "SoundLoopMusicTrack",
                                                                 "SoundPlayAmbientTrack",
                                                                 "SoundPlayCommand",
                                                                 "SoundPlayMusicTrack",
                                                                 "SoundSetMusicVolume",
                                                                 "SoundStopAmbientTrack",
                                                                 "SoundStopMusicTrack",
                                                                 "SndSetNIDuck",
                                                                 "SndSetPitchMod"};

/// The voice set a human of character type `type` speaks with: its `CfgChar` record's voice (argument 12, the type's
/// `+0x118`) among `recorded`'s calls; -1 when the type has none.
///
/// **Coney's stand-in:** the type's own record; the alias rule that gives a player its class's record
/// (docs/research/characters.md) and `HuSetVoiceIndex` are not applied.
[[nodiscard]] int voiceSetOfType(const RecordedCalls& recorded, int type);

/// Registers kSoundBindings in `vm`, working on `context.sound` as it is at each call (null plays nothing: the music
/// bindings then go to `context.host`, the speech bindings run their callbacks at once). Callbacks are called through
/// `scripts`. Both must outlive the state.
///
/// Research: docs/research/sound.md, docs/references/bindings/sound.md, docs/references/bindings/character.md#huspeak
void addSoundBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context);

} // namespace coney::script
