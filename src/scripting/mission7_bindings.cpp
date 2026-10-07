// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/mission7_bindings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/name_hash.h"
#include "effects/level_effects.h"
#include "effects/room_smoke.h"
#include "hud/hud.h"
#include "hud/scripted_bars.h"
#include "scenes/scene_player.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "scripting/sound_bindings.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/cars.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as a handle (an unsigned integer).
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(wholeArg(args, i)));
}

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].isNil()) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// Argument `i` as a string; empty for nil or a non-string.
std::string stringArg(std::span<const Value> args, std::size_t i) {
    return i < args.size() && args[i].type() == Value::Type::String ? binding::string(args, i) : std::string();
}

// The character bindings' host of the level, if any.
HumanBindingHost* humansOf(const BindingContext& context) {
    return context.ai != nullptr ? context.ai->humans() : nullptr;
}

// Whether a human has `handle`: a bad handle skips its line in HuActionDialog.
bool knownHuman(const BindingContext& context, double handle) {
    return context.humans != nullptr && context.humans->find(handle) != nullptr;
}

// One line of HuActionDialog, said as HuSpeakNI says it (cutting off the speaker's line, no look-at target); when no
// line plays the callback runs at once, as HuSpeakNI's does.
void sayLine(ScriptSystem& scripts, const BindingContext& context, double human, const std::string& line,
             const std::string& callback) {
    const bool speechOff = context.scenes != nullptr && context.scenes->cinematicActive();
    const SpeechCall call{.human = human, .line = line, .interrupt = true, .lookAt = 0.0};
    const bool said =
        !line.empty() && !speechOff && context.sound != nullptr && context.sound->speak(call, callback, std::nullopt);
    if (!said && !callback.empty()) {
        scripts.call(callback, {});
    }
}

// A sound SoundPreLoad set up: its name's hash and where it will play.
struct PreloadedSound {
    std::uint32_t sound = 0;
    std::array<float, 3> position{};
};

} // namespace

void addMission7Bindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context,
                         const std::function<double()>& nextHandle) {
    // ---- The humans.
    // `HuActionDialog(a, lineA, b, lineB, callback)`: `a` says its line with no callback, then `b` its line with the
    // callback (both issued now). A bad handle skips its line, and for `b` the callback too. **Coney stand-in**: the
    // conversation partner each is given (human `+0x18c`) has no reader on the page, so it is not kept.
    // @orig 0x00239788 Human_ActionDialog (unknown)
    vm.registerFunction("HuActionDialog", [scripts = &scripts, context = &context](std::span<const Value> args) {
        const double a = handleArg(args, 0);
        const double b = handleArg(args, 2);
        if (knownHuman(*context, a)) {
            sayLine(*scripts, *context, a, stringArg(args, 1), {});
        }
        if (knownHuman(*context, b)) {
            sayLine(*scripts, *context, b, stringArg(args, 3), stringArg(args, 4));
        }
        return binding::none();
    });
    // `HuIsTagging(human)`: true while it sprays a tag.
    // @orig 0x002387e8 Human_IsTagging (unknown)
    vm.registerFunction("HuIsTagging", [context = &context](std::span<const Value> args) {
        const HumanBindingHost* host = humansOf(*context);
        return binding::boolean(host != nullptr && host->tagging(handleArg(args, 0)));
    });
    // `HuMarkReachable(human, reachable)`.
    // @orig 0x00239e30 Human_MarkReachable (unknown)
    vm.registerFunction("HuMarkReachable", [context = &context](std::span<const Value> args) {
        if (HumanBindingHost* host = humansOf(*context); host != nullptr) {
            host->markReachable(handleArg(args, 0), boolArg(args, 1));
        }
        return binding::none();
    });
    // `HuSetRageMode(human, on)` (Human::setRageMode()).
    vm.registerFunction("HuSetRageMode", [context = &context](std::span<const Value> args) {
        if (HumanBindingHost* host = humansOf(*context); host != nullptr) {
            host->setRageMode(handleArg(args, 0), boolArg(args, 1));
        }
        return binding::none();
    });
    // `ActTurnTo(human, target, turnSpeed, timeMs)`: the point is read only with a second argument; the delay is -1
    // (a random 0-500 ms) when left out.
    // @orig 0x002fdf70 Action_TurnToTarget (unknown)
    vm.registerFunction("ActTurnTo", [context = &context](std::span<const Value> args) {
        const std::optional<std::array<float, 3>> point = binding::position(args, 1);
        if (context->ai != nullptr && point) {
            context->ai->actTurnTo(TurnToCall{
                .human = handleArg(args, 0),
                .point = *point,
                .turn = static_cast<float>(binding::number(args, 2)),
                .delayMs = static_cast<std::int16_t>(args.size() > 3 && !args[3].isNil() ? wholeArg(args, 3) : -1)});
        }
        return binding::none();
    });

    // ---- The game state.
    // `CfgWarriorWeapons(enabled)` (StoryState::warriorWeapons).
    // @orig 0x0041d770 GameState_SetWarriorWeapons (unknown)
    vm.registerFunction("CfgWarriorWeapons", [context = &context](std::span<const Value> args) {
        if (context->state != nullptr) {
            context->state->story.warriorWeapons = boolArg(args, 0);
        }
        return binding::none();
    });

    // ---- The world's objects.
    // `CarRemovePart(car, part, removed)` (Cars::removePart()); a handle that is not a car does nothing.
    // @orig 0x0038dfe0 Car_RemovePart (unknown)
    vm.registerFunction("CarRemovePart", [context = &context](std::span<const Value> args) {
        if (context->cars != nullptr) {
            context->cars->removePart(handleArg(args, 0), static_cast<std::uint32_t>(wholeArg(args, 1)),
                                      boolArg(args, 2));
        }
        return binding::none();
    });
    // `ObjEnablePhysics(object, enable)`: a live object's body made or freed (SpawnRecord::physicsBody); a handle with
    // no live object does nothing.
    // @orig 0x00396a90 Obj_EnablePhysics (unknown)
    vm.registerFunction("ObjEnablePhysics", [context = &context](std::span<const Value> args) {
        if (context->spawnRecords != nullptr) {
            if (world_objects::SpawnRecord* record = context->spawnRecords->find(handleArg(args, 0));
                record != nullptr && record->live && !record->removed) {
                record->physicsBody = boolArg(args, 1);
            }
        }
        return binding::none();
    });

    // ---- The room smoke.
    // `StartRoomSmoke(colour, amount)`: `colour` is {r, g, b, lowest alpha, highest alpha}, each 0-255 (a missing one
    // is 0). Coney has one view, so one overlay.
    // @orig 0x0018e070 ScreenFx_StartRoomSmoke (unknown)
    vm.registerFunction("StartRoomSmoke", [context = &context](std::span<const Value> args) {
        if (context->effects == nullptr) {
            return binding::none();
        }
        std::array<std::uint8_t, 5> colour{};
        if (!args.empty() && args[0].table() != nullptr) {
            for (std::size_t k = 0; k < colour.size(); ++k) {
                const double value = args[0].table()->get(Value(static_cast<double>(k + 1))).number().value_or(0.0);
                colour.at(k) = static_cast<std::uint8_t>(static_cast<std::int64_t>(std::trunc(value)) & 0xff);
            }
        }
        context->effects->smoke.start(
            effects::RoomSmokeSettings{.tint = {colour[0], colour[1], colour[2]},
                                       .lowestAlpha = colour[3],
                                       .highestAlpha = colour[4],
                                       .amount = static_cast<float>(binding::number(args, 1))});
        return binding::none();
    });
    // `EndRoomSmoke()`.
    // @orig 0x0018e0f8 ScreenFx_EndRoomSmoke (unknown)
    vm.registerFunction("EndRoomSmoke", [context = &context](std::span<const Value> /*args*/) {
        if (context->effects != nullptr) {
            context->effects->smoke.stop();
        }
        return binding::none();
    });

    // ---- The HUD's scripted bars (hud::ScriptedBars).
    // `HUDEnableBar(kind, on, labels, count, flag, texA, texB)`: `labels` up to four strings; `texA` and `texB` default
    // to the chase gauge's icon and track.
    vm.registerFunction("HUDEnableBar", [context = &context](std::span<const Value> args) {
        if (context->hud == nullptr) {
            return binding::none();
        }
        std::vector<std::string> labels;
        if (args.size() > 2 && args[2].table() != nullptr) {
            for (std::size_t k = 0; k < hud::kGenericBars; ++k) {
                const Value label = args[2].table()->get(Value(static_cast<double>(k + 1)));
                labels.push_back(label.type() == Value::Type::String ? std::string(*label.string()) : std::string());
            }
        }
        const auto word = [args](std::size_t i, std::uint32_t fallback) {
            return i < args.size() && !args[i].isNil() ? static_cast<std::uint32_t>(wholeArg(args, i)) : fallback;
        };
        context->hud->bars().enable(static_cast<int>(wholeArg(args, 0)), boolArg(args, 1), labels,
                                    static_cast<std::uint32_t>(wholeArg(args, 3)), boolArg(args, 4),
                                    word(5, hud::ChaseGauge{}.endIcon), word(6, hud::ChaseGauge{}.track));
        return binding::none();
    });
    // `HUDSetBarPercentage(kind, fill, fill2, index, value2)`.
    vm.registerFunction("HUDSetBarPercentage", [context = &context](std::span<const Value> args) {
        if (context->hud != nullptr) {
            const auto number = [args](std::size_t i) {
                return i < args.size() && !args[i].isNil() ? static_cast<float>(binding::number(args, i)) : 0.0F;
            };
            context->hud->bars().setPercentage(static_cast<int>(wholeArg(args, 0)), number(1), number(2),
                                               static_cast<std::uint32_t>(wholeArg(args, 3)), number(4));
        }
        return binding::none();
    });
    // `HUDSetBarProperty(index, gradient, rgba, width)`: each channel's low byte is kept.
    vm.registerFunction("HUDSetBarProperty", [context = &context](std::span<const Value> args) {
        if (context->hud == nullptr) {
            return binding::none();
        }
        std::array<std::uint8_t, 4> rgba{0, 0, 0, 255};
        if (args.size() > 2 && args[2].table() != nullptr) {
            for (std::size_t k = 0; k < rgba.size(); ++k) {
                if (const std::optional<double> value =
                        args[2].table()->get(Value(static_cast<double>(k + 1))).number();
                    value) {
                    rgba.at(k) = static_cast<std::uint8_t>(static_cast<std::int64_t>(std::trunc(*value)) & 0xff);
                }
            }
        }
        context->hud->bars().setProperty(static_cast<std::uint32_t>(wholeArg(args, 0)), boolArg(args, 1),
                                         graphics::Rgba{rgba[0], rgba[1], rgba[2], rgba[3]},
                                         static_cast<float>(binding::number(args, 3)));
        return binding::none();
    });

    // ---- The preloaded sounds.
    // `SoundPreLoad(sound, pos)` sets a positional sound up without starting it and returns its handle; `SoundStart
    // (handle)` starts it where it was set up (a handle that names no prepared sound does nothing). **Coney's
    // reading**: Coney's sounds start with no loading delay, so the preload only keeps the sound and the point until
    // the start; a sound started once is forgotten (the original's task ends with its sound).
    // @orig 0x00113700 Audio_PreloadSoundAt (unknown)
    // @orig 0x00113780 Audio_StartPreloadedSound (unknown)
    auto preloaded = std::make_shared<std::map<double, PreloadedSound>>();
    vm.registerFunction("SoundPreLoad", [preloaded, nextHandle](std::span<const Value> args) {
        const double handle = nextHandle();
        (*preloaded)[handle] = PreloadedSound{.sound = crc32(stringArg(args, 0)),
                                              .position = binding::position(args, 1).value_or(std::array<float, 3>{})};
        return binding::number(handle);
    });
    vm.registerFunction("SoundStart", [context = &context, preloaded](std::span<const Value> args) {
        const auto found = preloaded->find(handleArg(args, 0));
        if (found == preloaded->end()) {
            return binding::none();
        }
        const PreloadedSound sound = found->second;
        preloaded->erase(found);
        if (context->sound != nullptr) {
            static_cast<void>(context->sound->play3D(sound.sound, sound.position));
        }
        return binding::none();
    });
}

} // namespace coney::script
