// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "human/human_sounds.h"
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
    std::uint32_t soundHash = 0; ///< The sound's hash when `index` is -1 and `sound` is empty (an unnamed sound).
    std::uint32_t count = 0;     ///< How many table entries from `index` it picks from.
    float range = -1.0F;         ///< Metres from `from` (-1: the first sound's far distance + 10).
    int plays = -1;              ///< Plays before it switches itself off (-1 without limit).
    std::uint32_t minDelay = 0;  ///< The shortest pause between plays, whole seconds.
    std::uint32_t maxDelay = 0;  ///< The longest pause, whole seconds.
    std::uint8_t mode = 0;       ///< How it plays (docs/research/sound.md#ambient).
    std::uint8_t filter = 0;     ///< Which players hear it: 0 off covered ground, 1 on it, 2 all (larger: 0).
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

/// A human's sound as gameplay hands it to the game's sound (docs/research/sound-events.md): what his update asked for
/// and what choosing its sounds reads of him.
struct HumanSoundCall {
    double human = 0.0;              ///< His script handle (the sounds' owner, his lines' speaker).
    int characterType = -1;          ///< His `HuCreate` type (its `CfgChar` record: voice set, women's voices, class).
    std::array<float, 3> position{}; ///< His feet.
    float headingDegrees = 0.0F;     ///< His facing, degrees (0 along +y).
    bool player = false;             ///< A player (human `+0x1b0` not -1).
    std::uint32_t ground = 0;        ///< The ground's material under him (`+0x1d8`); 0 the default.
    bool hiddenInShadow = false;     ///< Sneaking in shadow (state `0x200000`): his footsteps at half volume.
    bool combatFraming = false;      ///< The camera frames his fight (`Camera_IsCombatFraming`).
    bool burning = false;            ///< He burns (`+0x19b`).
    bool targetIsPlayer = false;     ///< His target is a player (some lines louder).
    bool hasThrowTarget = false;     ///< He has a target to throw at (a human or a flag).
    std::optional<std::uint32_t> heldMaterial; ///< The material of the world object in his hand (type `+100`).
    std::uint32_t heldModel = 0;               ///< That object's model hash (`+0xc4`).
    int heldType = 0;                          ///< That object's type (`+0x86`; 8 a Molotov).
    human::HumanSound sound;                   ///< What he asked for.
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

    // ---- The humans' sounds (docs/research/sound-events.md) ----

    /// A human's animation sound or hit sound, played as the game chooses it. Ignored by default.
    virtual void humanSound(const HumanSoundCall& /*call*/) {}

    // ---- The sound matrix (docs/research/sound.md#sound-matrix); a host without one ignores them ----

    /// `SndLoadMatrix(name)`: whether the name is new (the matrix emptied, `<name>_preload.lua` to run).
    virtual bool loadSoundMatrix(std::string_view /*name*/) { return true; }
    /// `NewMaterialSlots(m1, m2, count, columns, v1, v2, v3)`.
    virtual void newMaterialSlots(std::uint32_t /*m1*/, std::uint32_t /*m2*/, std::uint32_t /*count*/,
                                  std::uint32_t /*columns*/, const std::array<float, 3>& /*volumes*/) {}
    /// `NewMaterialSound(i, m1, m2, s1, s2, s3)`: each sound's hash, 0 for `none`, nullopt to leave the column.
    virtual void newMaterialSound(std::uint32_t /*index*/, std::uint32_t /*m1*/, std::uint32_t /*m2*/,
                                  const std::array<std::optional<std::uint32_t>, 3>& /*sounds*/) {}
    /// `SetNumberOfMaterialSlots(m1, m2, count)`.
    virtual void setMaterialSlotCount(std::uint32_t /*m1*/, std::uint32_t /*m2*/, std::uint32_t /*count*/) {}
    /// `DuplicateSoundMaterials(a, b)`.
    virtual void duplicateSoundMaterials(std::uint32_t /*a*/, std::uint32_t /*b*/) {}
    /// `NewAnimSlots(event, count, columns, v1, v2, v3)`.
    virtual void newAnimSlots(std::uint32_t /*event*/, std::uint32_t /*count*/, std::uint32_t /*columns*/,
                              const std::array<float, 3>& /*volumes*/) {}
    /// `NewAnimSound(i, event, s1, s2, s3)`, as newMaterialSound().
    virtual void newAnimSound(std::uint32_t /*index*/, std::uint32_t /*event*/,
                              const std::array<std::optional<std::uint32_t>, 3>& /*sounds*/) {}

    // ---- Ambience ----

    /// `AddAmbientSound(index, sound)`: an entry of the ambient table.
    virtual void addAmbientSound(int index, std::uint32_t sound) = 0;
    /// `AddAmbientSoundEmitter2`: the emitter's id.
    virtual double addAmbientEmitter(const AmbientEmitterCall& call) = 0;
    /// `SetAmbientEmitterPositions(name, ...)`: the named emitter's positions (at most five).
    virtual void setAmbientEmitterPositions(std::string_view name, std::span<const std::array<float, 3>> positions) = 0;
    /// `SoundPlayAmbientTrack(name)`.
    virtual void playAmbientTrack(std::uint32_t sound) = 0;
    /// `SoundPlay2D(name)`: the sound played once without a position; its handle, or NilSoundHandle (0) when it did not
    /// start.
    virtual double play2D(std::uint32_t sound) = 0;
    /// `SoundPlay(name, pos)` and `PlaySound3D(sound, position)` (`0x0010fdd0`, as a radio plays its streams): the
    /// sound played once at a point in the world (full volume, normal pitch); its handle, or NilSoundHandle (0) when
    /// it did not start. A host with no positional sound plays nothing (the default).
    virtual double play3D(std::uint32_t /*sound*/, const std::array<float, 3>& /*position*/) { return 0.0; }
    /// `EnableAmbientEmitter(id, on)`: switches an ambient emitter on or off. Does nothing by default.
    virtual void enableAmbientEmitter(int /*emitter*/, bool /*on*/) {}
    /// `SetAmbientEmitterVolumeMod(id, volume)`: an ambient emitter's volume. Does nothing by default.
    virtual void setAmbientEmitterVolume(int /*emitter*/, float /*volume*/) {}
    /// Whether player 1 stands on covered ground, which decides the ambient emitters he hears
    /// (docs/research/sound-events.md#covered). Does nothing by default.
    virtual void setPlayerCovered(bool /*covered*/) {}
    /// Whether the camera frames player 1's fight (`Camera_IsCombatFraming`, `0x00233c50`: L1 held at a fight target,
    /// docs/research/sound-events.md#players), each frame: the angry breathing follows it. Does nothing by default.
    virtual void setCombatFraming(bool /*framing*/) {}
    /// An AI noise was reported (a strike on a world object or a car): the ambient manager's event stamp, which opens
    /// the `_DAM_` emitters' window from 2 s to 15 s after it (docs/research/sound.md#ambient). Does nothing by
    /// default.
    virtual void markAmbientEvent() {}
    /// `SoundStopAmbientTrack()`.
    virtual void stopAmbientTrack() = 0;
    /// Whether the sound `handle` (from play2D() or play3D()) still plays.
    [[nodiscard]] virtual bool soundPlaying(double /*handle*/) const { return false; }
    /// Stops the sound `handle`.
    virtual void stopSound(double /*handle*/) {}
    /// Moves the positional sound `handle` and sets its volume (0-1).
    virtual void moveSound(double /*handle*/, const std::array<float, 3>& /*position*/, float /*volume*/) {}
    /// `SoundPauseSound(on)`: pauses every sound playing now where it is (`on`), or resumes them.
    virtual void pauseSound(bool on) = 0;
    /// `SetAmbientTrackVolume(volume)`, 0-1.
    virtual void setAmbientTrackVolume(float volume) = 0;

    // ---- Music and the listener ----

    /// `SoundPlayMusicTrack` (`loop` false, with the end `callback`) and `SoundLoopMusicTrack` (`loop` true).
    virtual void playMusic(std::uint32_t track, bool loop, std::string_view callback) = 0;
    /// The system music's pick: `track` looping, cross-faded over `fadeBars` bars (0: a cut at the bar). By default
    /// playMusic() without a fade.
    virtual void playSystemMusic(std::uint32_t track, int /*fadeBars*/) { playMusic(track, true, {}); }
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
inline constexpr std::array<std::string_view, 33> kSoundBindings{"AddAmbientSound",
                                                                 "AddAmbientSoundEmitter",
                                                                 "SetAmbientEmitterVolumeMod",
                                                                 "AddAmbientSoundEmitter2",
                                                                 "HuSay",
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
                                                                 "SoundPauseSound",
                                                                 "SoundPlay2D",
                                                                 "SoundPlayAmbientTrack",
                                                                 "SoundPlayCommand",
                                                                 "SoundPlayMusicTrack",
                                                                 "SoundSetMusicVolume",
                                                                 "SoundStopAmbientTrack",
                                                                 "SoundStopMusicTrack",
                                                                 "SndSetNIDuck",
                                                                 "SndSetPitchMod",
                                                                 "NewMaterialSlots",
                                                                 "NewMaterialSound",
                                                                 "DuplicateSoundMaterials",
                                                                 "SetNumberOfMaterialSlots",
                                                                 "NewAnimSlots",
                                                                 "NewAnimSound"};

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
