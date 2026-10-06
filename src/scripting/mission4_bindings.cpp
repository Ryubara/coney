// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/mission4_bindings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "animation/anim_math.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/story_bindings.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/level_objects.h"
#include "world_objects/nav_links.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// The most a call-for-help chance can be, percent.
constexpr std::uint32_t kMaxChance = 100;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as an unsigned integer.
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(wholeArg(args, i));
}

// Argument `i` as a handle.
double handleArg(std::span<const Value> args, std::size_t i) { return static_cast<double>(unsignedArg(args, i)); }

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].isNil()) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// Argument `i` as a boolean that is `fallback` when omitted.
bool boolArgOr(std::span<const Value> args, std::size_t i, bool fallback) {
    return i >= args.size() ? fallback : boolArg(args, i);
}

// Argument `i` as a string; empty for nil or a non-string.
std::string stringArg(std::span<const Value> args, std::size_t i) {
    return i < args.size() && args[i].string() ? binding::string(args, i) : std::string();
}

// The story host of the level, if any.
StoryBindingHost* storyOf(const BindingContext& context) {
    return context.ai != nullptr ? context.ai->story() : nullptr;
}

// `ObjGetIndex(name)`: the type's index in the object database, -1 for an unknown or nil name.
// @orig 0x00397780 ObjType_FindIndex (unknown)
binding::Results objGetIndex(const BindingContext& context, std::span<const Value> args) {
    const std::string name = stringArg(args, 0);
    if (context.objectTypes == nullptr || name.empty()) {
        return binding::number(-1.0);
    }
    const world_objects::ObjectType* type = context.objectTypes->find(name);
    return binding::number(type != nullptr ? static_cast<double>(type->index) : -1.0);
}

// `GetRTTI(object)`: the object's type bits. Coney stand-in: only the bits the page names are given, one per kind of
// object Coney keeps (a human, a flag, a spawned prop); a tag and every other object give 0, as a bad handle does.
// @orig 0x00385950 Object_GetTypeBits (unknown)
binding::Results getRtti(const BindingContext& context, std::span<const Value> args) {
    const double handle = handleArg(args, 0);
    std::uint32_t bits = 0;
    if (context.humans != nullptr && context.humans->find(handle) != nullptr) {
        bits = rtti::kHuman;
    } else if (context.flags != nullptr && context.flags->find(handle) != nullptr) {
        bits = rtti::kFlag;
    } else if (context.spawnRecords != nullptr && context.spawnRecords->find(handle) != nullptr) {
        bits = rtti::kProp;
    }
    return binding::number(static_cast<double>(bits));
}

// `ChangeBlocker(pos, open)`: the walkable area at the point blocked or opened.
binding::Results changeBlocker(const BindingContext& context, std::span<const Value> args) {
    const std::optional<std::array<float, 3>> at = binding::position(args, 0);
    if (context.objects != nullptr && at) {
        world_objects::NavLinks links(context.objects->world.paths);
        static_cast<void>(links.changeBlocker(anim::Vec3{(*at)[0], (*at)[1], (*at)[2]}, boolArg(args, 1)));
    }
    return binding::none();
}

} // namespace

void addMission4Bindings(LuaVm& vm, const BindingContext& context) {
    // ---- The objects.
    vm.registerFunction("ObjGetIndex",
                        [context = &context](std::span<const Value> args) { return objGetIndex(*context, args); });
    vm.registerFunction("GetRTTI", [context = &context](std::span<const Value> args) { return getRtti(*context, args); });
    vm.registerFunction("ChangeBlocker",
                        [context = &context](std::span<const Value> args) { return changeBlocker(*context, args); });

    // ---- The game state.
    // `CfgChanceToGetHelp(percent)`: above 100 is 100.
    // @orig 0x00294888 Cfg_SetChanceToGetHelp (unknown)
    vm.registerFunction("CfgChanceToGetHelp", [context = &context](std::span<const Value> args) {
        if (context->state != nullptr) {
            context->state->story.chanceToGetHelp = static_cast<int>(std::min(unsignedArg(args, 0), kMaxChance));
        }
        return binding::none();
    });
    // `ForceCrimeLevel(on)`: on by default; a wanted gang stays wanted while it is set. Coney stand-in: the police
    // gang's one-way hostility while it is set (0x0016cdf0) is not built.
    // @orig 0x0041d8d0 GameState_SetForceCrimeLevel (unknown)
    vm.registerFunction("ForceCrimeLevel", [context = &context](std::span<const Value> args) {
        if (context->state != nullptr) {
            context->state->player.crimes.setForced(boolArgOr(args, 0, true));
        }
        return binding::none();
    });

    // ---- The brains and humans.
    vm.registerFunction("BrSetPedType", [context = &context](std::span<const Value> args) {
        if (StoryBindingHost* host = storyOf(*context); host != nullptr) {
            host->setPedType(handleArg(args, 0), static_cast<std::uint16_t>(unsignedArg(args, 1) & 0xffffU));
        }
        return binding::none();
    });
    // ---- The gangs' spawners: -1 for the gang does nothing.
    vm.registerFunction("GangSetMaxConcurrent", [context = &context](std::span<const Value> args) {
        const int gang = static_cast<int>(wholeArg(args, 0));
        if (StoryBindingHost* host = storyOf(*context); host != nullptr && gang != -1) {
            host->setSpawnerMaxConcurrent(gang, stringArg(args, 1), static_cast<int>(wholeArg(args, 2)));
        }
        return binding::none();
    });
    vm.registerFunction("GangSetSpawnerMustBeOffScreen", [context = &context](std::span<const Value> args) {
        const int gang = static_cast<int>(wholeArg(args, 0));
        if (StoryBindingHost* host = storyOf(*context); host != nullptr && gang != -1) {
            host->setSpawnerOffScreen(gang, stringArg(args, 1), boolArg(args, 2));
        }
        return binding::none();
    });
    vm.registerFunction("HuSetWounded", [context = &context](std::span<const Value> args) {
        if (StoryBindingHost* host = storyOf(*context); host != nullptr) {
            host->setWounded(handleArg(args, 0), boolArg(args, 1));
        }
        return binding::none();
    });
}

} // namespace coney::script
