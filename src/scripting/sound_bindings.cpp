// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/sound_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "core/name_hash.h"
#include "scenes/scene_player.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "warriors/created_humans.h"

namespace coney::script {

namespace {

// The `CfgChar` argument that holds the voice set (argument 12, the type's +0x118, 0-based 11).
constexpr std::size_t kCfgCharVoice = 11;
// Positions `SetAmbientEmitterPositions` takes.
constexpr std::size_t kEmitterPositions = 5;
// Coney's NilHandle (script_bindings.cpp): a look-at target that names nobody.
constexpr double kNilHandle = 0.0;

// Argument `i` as an integer, truncated as tolua reads one.
int intArg(std::span<const Value> args, std::size_t i) {
    return static_cast<int>(std::trunc(binding::number(args, i)));
}

// Argument `i` as an unsigned integer, truncated as tolua reads one (a negative number wraps).
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, i))));
}

// Argument `i` as a float, as tolua reads a single-precision number.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Argument `i` as a boolean with a default: `fallback` when the argument is absent; nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i, bool fallback) {
    if (i >= args.size()) {
        return fallback;
    }
    if (args[i].isNil()) {
        return false;
    }
    return args[i].type() != Value::Type::Number || binding::number(args, i) != 0.0;
}

// A handle argument with a default for an absent one: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i, double fallback = 0.0) {
    if (i >= args.size() || args[i].isNil()) {
        return fallback;
    }
    return static_cast<double>(unsignedArg(args, i));
}

// The callback name argument `i`: empty for nil or a missing one.
std::string callbackArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].type() != Value::Type::String) {
        return {};
    }
    return binding::string(args, i);
}

// A position argument, the origin when it is not a table of three numbers.
std::array<float, 3> positionArg(std::span<const Value> args, std::size_t i) {
    return binding::position(args, i).value_or(std::array<float, 3>{});
}

// A sound named by string argument `i`, or given as its hash by a number (the music bindings' overloads).
std::uint32_t soundArg(std::span<const Value> args, std::size_t i) {
    if (i < args.size() && args[i].type() == Value::Type::Number) {
        return unsignedArg(args, i);
    }
    return crc32(binding::string(args, i));
}

// Calls the callback `name` (none when empty) with `arg` (none when nothing).
void runCallback(ScriptSystem& scripts, const std::string& name, std::optional<double> arg) {
    if (name.empty()) {
        return;
    }
    if (arg) {
        const std::array<Value, 1> callArgs{Value(*arg)};
        scripts.call(name, callArgs);
    } else {
        scripts.call(name, {});
    }
}

// Whether speech is off: a playing cinematic holds the scene state (game state +0x410), read by HuSpeak and the
// command lines (docs/research/sound.md#speech).
bool speechOff(const BindingContext& context) { return context.scenes != nullptr && context.scenes->cinematicActive(); }

// Whether a script silenced the human's speech commands (`HuEnableSoundCommands`, the byte `Human_SayCommand` reads).
bool commandsSilenced(const BindingContext& context, double human) {
    HumanBindingHost* humans = context.ai != nullptr ? context.ai->humans() : nullptr;
    if (humans == nullptr) {
        return false;
    }
    const std::optional<HumanStatus> status = humans->status(human);
    return status.has_value() && !status->soundCommands;
}

// A binding that hands its arguments to the sound host when there is one and returns nothing. The host is read at each
// call: main gives it once the audio has started.
template <typename Body> NativeFunction soundCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (SoundHost* sound = context->sound; sound != nullptr) {
            body(*sound, args);
        }
        return binding::none();
    };
}

// `HuSpeak(human, line, callback, arg, flag, listener)` and `HuSpeakNI` (`interrupt`): the line plays at the speaker
// and the callback runs when it ends, or at once when no line plays (no human or line, speech off, a line playing for
// HuSpeak). The callback gets `arg` unless it is 0. `flag` is passed on in the original with a meaning not traced;
// Coney does not read it.
// @orig 0x00364e48 HuSpeak (unknown)
// @orig 0x00239370 Human_Speak (unknown)
// @orig 0x00364f50 HuSpeakNI (unknown)
// @orig 0x002395a0 Human_SpeakInterrupt (unknown)
NativeFunction makeHuSpeak(ScriptSystem& scripts, const BindingContext& context, bool interrupt) {
    return [scripts = &scripts, context = &context, interrupt](std::span<const Value> args) {
        const std::string callback = callbackArg(args, 2);
        const int arg = intArg(args, 3);
        const std::optional<double> passed = arg != 0 ? std::optional<double>(arg) : std::nullopt;
        const SpeechCall call{.human = handleArg(args, 0),
                              .line = binding::string(args, 1),
                              .interrupt = interrupt,
                              .lookAt = handleArg(args, 5, kNilHandle)};
        SoundHost* sound = context->sound;
        const bool said =
            !call.line.empty() && !speechOff(*context) && sound != nullptr && sound->speak(call, callback, passed);
        if (!said) {
            runCallback(*scripts, callback, passed);
        }
        return binding::none();
    };
}

// `HuShutUp(human, force)`.
// @orig 0x00364ba8 HuShutUp (unknown)
// @orig 0x00239558 Human_ShutUp (unknown)
NativeFunction makeHuShutUp(const BindingContext& context) {
    return soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
        sound.shutUp(handleArg(args, 0), boolArg(args, 1, false));
    });
}

// `SoundPlayCommand(human, command, callback, interrupt, target, flag2)`: the line's handle, or NilSoundHandle (0)
// when no line plays; then the callback runs at once with the speaker's handle. `flag2` (meaning not traced) is not
// read.
// @orig 0x00372fe0 SoundPlayCommand (unknown)
// @orig 0x001140d8 Sound_PlayCommand (unknown)
NativeFunction makeSoundPlayCommand(ScriptSystem& scripts, const BindingContext& context) {
    return [scripts = &scripts, context = &context](std::span<const Value> args) {
        const double human = handleArg(args, 0);
        const std::string callback = callbackArg(args, 2);
        int voiceSet = -1;
        if (context->humans != nullptr && context->recorded != nullptr) {
            if (const HumanCreation* made = context->humans->find(human); made != nullptr) {
                voiceSet = voiceSetOfType(*context->recorded, made->type);
            }
        }
        const CommandCall call{.human = human,
                               .voiceSet = voiceSet,
                               .command = unsignedArg(args, 1),
                               .interrupt = boolArg(args, 3, true),
                               .target = handleArg(args, 4, kNilHandle)};
        std::optional<double> line;
        if (SoundHost* sound = context->sound;
            sound != nullptr && !speechOff(*context) && !commandsSilenced(*context, human)) {
            line = sound->sayCommand(call, callback);
        }
        if (!line) {
            runCallback(*scripts, callback, human);
        }
        return binding::number(line.value_or(0.0));
    };
}

// `AddAmbientSoundEmitter2(name, pos1, pos2, index, sound, count, range, arg8, minDelay, maxDelay, arg11, mode)`: the
// emitter's id (0 without a sound host). A mode above 2 becomes 0.
// @orig 0x00371db0 AddAmbientSoundEmitter2 (unknown)
// @orig 0x00113920 Ambient_AddEmitter2 (unknown)
NativeFunction makeAddAmbientSoundEmitter2(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        SoundHost* sound = context->sound;
        if (sound == nullptr) {
            return binding::number(0.0);
        }
        const std::uint32_t mode = unsignedArg(args, 11);
        const AmbientEmitterCall call{.name = binding::string(args, 0),
                                      .from = positionArg(args, 1),
                                      .to = positionArg(args, 2),
                                      .index = intArg(args, 3),
                                      .sound = binding::string(args, 4),
                                      .count = unsignedArg(args, 5),
                                      .range = floatArg(args, 6),
                                      .arg8 = intArg(args, 7),
                                      .minDelay = unsignedArg(args, 8),
                                      .maxDelay = unsignedArg(args, 9),
                                      .arg11 = static_cast<std::uint8_t>(unsignedArg(args, 10)),
                                      .mode = static_cast<std::uint8_t>(mode > 2 ? 0 : mode)};
        return binding::number(sound->addAmbientEmitter(call));
    };
}

// `SetAmbientEmitterPositions(name, pos1, ..., pos5, count)`: the first `count` (at most five) positions.
// @orig 0x00372058 SetAmbientEmitterPositions (unknown)
// @orig 0x00113a58 Ambient_SetEmitterPositions (unknown)
NativeFunction makeSetAmbientEmitterPositions(const BindingContext& context) {
    return soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
        const std::size_t count = std::min<std::size_t>(unsignedArg(args, 6), kEmitterPositions);
        std::vector<std::array<float, 3>> positions;
        positions.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            positions.push_back(positionArg(args, 1 + i));
        }
        sound.setAmbientEmitterPositions(binding::string(args, 0), positions);
    });
}

// `SoundPlayMusicTrack(track, callback)` and `SoundLoopMusicTrack(track, flag)`, by name or hash. Without a sound host
// the request goes to the binding host's stand-in (its name only).
// @orig 0x00371230 SoundPlayMusicTrack (unknown)
// @orig 0x00113510 Sound_PlayMusicTrack (unknown)
// @orig 0x00371348 SoundLoopMusicTrack (unknown)
// @orig 0x00113590 Sound_LoopMusicTrack (unknown)
NativeFunction makePlayMusicTrack(const BindingContext& context, bool loop) {
    return [context = &context, loop](std::span<const Value> args) {
        if (SoundHost* sound = context->sound; sound != nullptr) {
            sound->playMusic(soundArg(args, 0), loop, loop ? std::string{} : callbackArg(args, 1));
        } else if (context->host != nullptr) {
            context->host->playMusic(binding::string(args, 0));
        }
        return binding::none();
    };
}

// `SoundStopMusicTrack()`.
// @orig 0x003713f8 SoundStopMusicTrack (unknown)
// @orig 0x001135e0 Sound_StopMusicTrack (unknown)
NativeFunction makeStopMusicTrack(const BindingContext& context) {
    return [context = &context](std::span<const Value> /*args*/) {
        if (SoundHost* sound = context->sound; sound != nullptr) {
            sound->stopMusic();
        } else if (context->host != nullptr) {
            context->host->stopMusic();
        }
        return binding::none();
    };
}

} // namespace

int voiceSetOfType(const RecordedCalls& recorded, int type) {
    for (const std::vector<Value>& call : recorded.calls("CfgChar")) {
        if (call.size() > kCfgCharVoice && intArg(call, 0) == type) {
            return intArg(call, kCfgCharVoice);
        }
    }
    return -1;
}

void addSoundBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context) {
    // The configuration the preloads make.
    // @orig 0x00113438 Sound_CfgMusicInfo (unknown)
    vm.registerFunction("SndCfgMusicInfo", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.configureMusicTrack(crc32(binding::string(args, 0)),
                                                      static_cast<float>(unsignedArg(args, 1)),
                                                      std::clamp(floatArg(args, 2), 0.0F, 1.0F));
                        }));
    // @orig 0x001141b0 Sound_CfgInterfaceSound (unknown)
    vm.registerFunction("SoundCfgInterfaceSound", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.setInterfaceSound(intArg(args, 0), crc32(binding::string(args, 1)));
                        }));
    // @orig 0x00113dd0 Sound_AllocateCharacterVoices (unknown)
    vm.registerFunction("SndAllocateCharacterVoices",
                        soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.allocateCharacterVoices(static_cast<int>(unsignedArg(args, 0)));
                        }));
    // @orig 0x00113df8 Sound_SetCommandSoundPercent (unknown)
    vm.registerFunction("SndSetCommandSoundPercent",
                        soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.setCommandSoundPercent(intArg(args, 0), unsignedArg(args, 1), unsignedArg(args, 2));
                        }));
    // @orig 0x001133d0 Sound_LoadBank (unknown)
    vm.registerFunction("SndLoadBank", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.loadSoundBank(binding::string(args, 0));
                        }));
    // @orig 0x001133b0 Sound_SetNIDuck (unknown)
    vm.registerFunction("SndSetNIDuck", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.setNonDuckableDuck(floatArg(args, 0));
                        }));
    // @orig 0x00113370 Sound_SetPitchMod (unknown)
    vm.registerFunction("SndSetPitchMod", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.setPitchFactor(floatArg(args, 0));
                        }));

    // The ambience.
    // @orig 0x00113810 Ambient_AddSound (unknown)
    vm.registerFunction("AddAmbientSound", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.addAmbientSound(intArg(args, 0), crc32(binding::string(args, 1)));
                        }));
    vm.registerFunction("AddAmbientSoundEmitter2", makeAddAmbientSoundEmitter2(context));
    vm.registerFunction("SetAmbientEmitterPositions", makeSetAmbientEmitterPositions(context));
    // @orig 0x00113608 Sound_PlayAmbientTrack (unknown)
    vm.registerFunction("SoundPlayAmbientTrack", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.playAmbientTrack(crc32(binding::string(args, 0)));
                        }));
    // `SoundPlay2D(name)`: the handle, NilSoundHandle (0) when nothing plays or there is no sound.
    // @orig 0x001137a8 Sound_Play2D (unknown)
    vm.registerFunction("SoundPlay2D", [context = &context](std::span<const Value> args) {
        SoundHost* sound = context->sound;
        return binding::number(sound != nullptr ? sound->play2D(crc32(binding::string(args, 0))) : 0.0);
    });
    // `SoundPauseSound(on)`: on defaults to true.
    // @orig 0x00114088 Audio_PauseSound (unknown)
    vm.registerFunction("SoundPauseSound", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.pauseSound(boolArg(args, 0, true));
                        }));
    // @orig 0x00113630 Sound_StopAmbientTrack (unknown)
    vm.registerFunction("SoundStopAmbientTrack",
                        soundCall(context, [](SoundHost& sound, std::span<const Value>) { sound.stopAmbientTrack(); }));
    // @orig 0x00113658 Sound_SetAmbientTrackVolume (unknown)
    vm.registerFunction("SetAmbientTrackVolume", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.setAmbientTrackVolume(std::clamp(floatArg(args, 0), 0.0F, 1.0F));
                        }));

    // The music and the listener.
    vm.registerFunction("SoundPlayMusicTrack", makePlayMusicTrack(context, false));
    vm.registerFunction("SoundLoopMusicTrack", makePlayMusicTrack(context, true));
    vm.registerFunction("SoundStopMusicTrack", makeStopMusicTrack(context));
    // @orig 0x001134b8 Sound_SetMusicVolume (unknown)
    vm.registerFunction("SoundSetMusicVolume", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.setMusicVolume(std::clamp(floatArg(args, 0), 0.0F, 1.0F));
                        }));
    // @orig 0x00114018 Sound_SetListener (unknown)
    vm.registerFunction("SndSetListener", soundCall(context, [](SoundHost& sound, std::span<const Value> args) {
                            sound.setListener(static_cast<int>(unsignedArg(args, 0)));
                        }));

    // The speech.
    vm.registerFunction("HuSpeak", makeHuSpeak(scripts, context, false));
    vm.registerFunction("HuSpeakNI", makeHuSpeak(scripts, context, true));
    // `HuSay(human, line)`: HuSpeak with no callback, no argument and no listener; nil plays nothing.
    // @orig 0x00239340 Human_Say (unknown)
    // @orig 0x00239370 Human_Speak (unknown)
    vm.registerFunction("HuSay", [context = &context](std::span<const Value> args) {
        const SpeechCall call{
            .human = handleArg(args, 0), .line = binding::string(args, 1), .interrupt = false, .lookAt = kNilHandle};
        if (SoundHost* sound = context->sound; sound != nullptr && !call.line.empty() && !speechOff(*context)) {
            static_cast<void>(sound->speak(call, {}, std::nullopt));
        }
        return binding::none();
    });
    vm.registerFunction("HuShutUp", makeHuShutUp(context));
    vm.registerFunction("SoundPlayCommand", makeSoundPlayCommand(scripts, context));
}

} // namespace coney::script
