// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/level_bindings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "world_objects/flags.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// The value of `NilHandle`, what a binding returns for no object (script_bindings.cpp sets the global).
constexpr double kNilHandle = 0.0;
// The heading `TeleportToFlag` reads as "the flag's own" (also its default).
constexpr int kFlagHeading = -1;
// The longest start callback name the original keeps (a 32-byte buffer).
constexpr std::size_t kCallbackNameLength = 31;

// Argument `i` truncated to a whole number, as tolua reads an integer.
int intArg(std::span<const Value> args, std::size_t i) {
    return static_cast<int>(std::trunc(binding::number(args, i)));
}

// The flags' locator over the context's humans: the live objects a flag can follow. Where the AI host has the human
// (a level in play), where it stands now; otherwise where the script made it or last teleported it.
world_objects::ObjectLocator locatorOf(const BindingContext& context) {
    return [context = &context](double handle) -> std::optional<world_objects::Placement> {
        if (context->humans == nullptr || handle == kNilHandle) {
            return std::nullopt;
        }
        if (context->ai != nullptr) {
            if (std::optional<world_objects::Placement> live = context->ai->humanPlacement(handle)) {
                return live;
            }
        }
        return context->humans->placement(handle);
    };
}

// `AddFlag(name, {x, y, z}, heading, kind, kind2)`: makes a flag in the pool and returns its handle. The position's
// three numbers are written back unchanged, so the table is left as it is.
// @orig 0x00379fd0 AddFlag (unknown)
NativeFunction makeAddFlag(const BindingContext& context, std::function<double()> nextHandle) {
    return [flags = context.flags, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        const double handle = nextHandle();
        if (flags != nullptr) {
            // tolua reads a missing coordinate as 0.
            const std::array<float, 3> position = binding::position(args, 1).value_or(std::array<float, 3>{});
            // The fourth argument is kept as 16 bits, the fifth read as 16 bits and sign-extended.
            const auto kind = static_cast<std::uint16_t>(intArg(args, 3));
            const auto kind2 = static_cast<std::int16_t>(intArg(args, 4));
            flags->add(handle, binding::string(args, 0), position, static_cast<float>(binding::number(args, 2)), kind,
                       kind2);
        }
        return binding::number(handle);
    };
}

// `FindFlag(name)`: the handle of the first flag of the pool with that name, or NilHandle.
// @orig 0x0037a770 FindFlag (unknown)
NativeFunction makeFindFlag(const BindingContext& context) {
    return [flags = context.flags](std::span<const Value> args) {
        const std::optional<double> found =
            flags != nullptr ? flags->findByName(binding::string(args, 0)) : std::nullopt;
        return binding::number(found.value_or(kNilHandle));
    };
}

// An M_Vector4 as the scripts index it (`.x`, `.y`, `.z`, `.w`): a table with those fields, w = 1. Coney's VM has no
// user types, so the table stands in for tolua's object.
binding::Results vectorResult(const std::array<float, 3>& p) {
    auto vector = std::make_shared<Table>();
    for (const auto& [field, value] :
         {std::pair{"x", p[0]}, std::pair{"y", p[1]}, std::pair{"z", p[2]}, std::pair{"w", 1.0F}}) {
        if (auto set = vector->set(Value(std::string(field)), Value(static_cast<double>(value))); !set) {
            return std::unexpected(set.error());
        }
    }
    return std::vector<Value>{Value(std::move(vector))};
}

// `GetFlagPos(flag)`: where the flag is (its parent's position when it follows a live object), as an M_Vector4.
// **Coney's choice** for a handle that names no flag (the page does not say): nil.
// @orig 0x0037a288 GetFlagPos (unknown)
// @orig 0x00416bb8 Flag_GetPosition (flags.cpp)
NativeFunction makeGetFlagPos(const BindingContext& context) {
    return [flags = context.flags, locate = locatorOf(context)](std::span<const Value> args) -> binding::Results {
        const world_objects::WorldFlag* flag = flags != nullptr ? flags->find(binding::number(args, 0)) : nullptr;
        if (flag == nullptr) {
            return std::vector<Value>{Value()};
        }
        return vectorResult(world_objects::WorldFlags::position(*flag, locate));
    };
}

// `GetPosition(object)`: where a world object is, as an M_Vector4; the origin for a handle that names none. Coney's
// world objects are the humans the scripts made (where they stand) and the flags.
// @orig 0x0036ca18 GetPosition (unknown)
// @orig 0x00385a50 Object_GetPosition (unknown)
NativeFunction makeGetPosition(const BindingContext& context) {
    return [flags = context.flags, locate = locatorOf(context)](std::span<const Value> args) -> binding::Results {
        const double handle = binding::number(args, 0);
        if (const std::optional<world_objects::Placement> human = locate(handle)) {
            return vectorResult(human->position);
        }
        if (const world_objects::WorldFlag* flag = flags != nullptr ? flags->find(handle) : nullptr) {
            return vectorResult(world_objects::WorldFlags::position(*flag, locate));
        }
        return vectorResult({});
    };
}

// `TeleportToFlag(object, flag, heading)`: puts the object on the flag's position, facing the flag's heading for -1
// (the default) or `heading` whole degrees, with no ground snap. Coney keeps the humans only, so it moves a human the
// scripts made and ignores any other handle (and a handle that names no flag).
// @orig 0x0036cdc0 TeleportToFlag (unknown)
// @orig 0x00385db0 Object_TeleportToFlag (unknown)
NativeFunction makeTeleportToFlag(const BindingContext& context) {
    return [context = &context, locate = locatorOf(context)](std::span<const Value> args) {
        const world_objects::WorldFlags* flags = context->flags;
        const world_objects::WorldFlag* flag = flags != nullptr ? flags->find(binding::number(args, 1)) : nullptr;
        HumanCreation* human = context->humans != nullptr ? context->humans->find(binding::number(args, 0)) : nullptr;
        if (flag == nullptr || human == nullptr) {
            return binding::none();
        }
        const int heading = args.size() > 2 ? intArg(args, 2) : kFlagHeading;
        human->teleported = world_objects::Placement{
            .position = world_objects::WorldFlags::position(*flag, locate),
            .headingDegrees = heading == kFlagHeading ? world_objects::WorldFlags::headingDegrees(*flag, locate)
                                                      : static_cast<float>(heading)};
        ++human->teleports;
        // A human in play is moved there too.
        if (context->ai != nullptr) {
            context->ai->humanTeleported(human->handle, *human->teleported);
        }
        return binding::none();
    };
}

// `CfgSetDatabaseSizes(objectTasks, worldFlags, boxes)`: makes the level's pools. Coney has the flags' pool and the
// spawn records' (world_objects/spawn_records.h); the arguments are also recorded for the boxes to come.
// @orig 0x0036bb10 CfgSetDatabaseSizes (unknown)
// @orig 0x0041d628 Cfg_SetDatabaseSizes (unknown)
NativeFunction makeCfgSetDatabaseSizes(const BindingContext& context) {
    return [flags = context.flags, records = context.spawnRecords,
            recorded = context.recorded](std::span<const Value> args) {
        if (recorded != nullptr) {
            recorded->add("CfgSetDatabaseSizes", args);
        }
        if (flags != nullptr) {
            // CfgSetDatabaseSizes passes the count plus 2; the pool adds 2 more (WorldFlags::kPoolExtra).
            flags->createPool(static_cast<std::size_t>(std::max(0, intArg(args, 1))));
        }
        if (records != nullptr) {
            records->createPool(static_cast<std::size_t>(std::max(0, intArg(args, 0))));
        }
        return binding::none();
    };
}

// The saved-number slot `slot` (1-8) as an index into the game state's slots; nothing outside them. The original reads
// the slot as 16 bits and checks no bounds (slot 9 is the saved flags' first word); **Coney's choice** is to read 0
// and drop writes outside the eight.
std::optional<std::size_t> saveSlot(int slot) {
    const auto wrapped = static_cast<std::int16_t>(slot);
    if (wrapped < 1 || static_cast<std::size_t>(wrapped) > GameState::kLuaSaveFloats) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(wrapped) - 1;
}

// `GetLUASaveDataFloat(slot)`: the saved script number in the slot; 0 until one is written (the game state's
// constructor zeroes them).
// @orig 0x0037b850 GetLUASaveDataFloat (unknown)
// @orig 0x0041ad00 GameState_GetLuaSaveFloat (unknown)
NativeFunction makeGetLuaSaveDataFloat(const BindingContext& context) {
    return [state = context.state](std::span<const Value> args) {
        const std::optional<std::size_t> slot = saveSlot(intArg(args, 0));
        return binding::number(slot ? static_cast<double>(state->luaSaveFloats.at(*slot)) : 0.0);
    };
}

// `SetLUASaveDataFloat(slot, value)`: stores a number, as a 32-bit float, in the slot.
// @orig 0x0037b7d8 SetLUASaveDataFloat (unknown)
// @orig 0x0041acd8 GameState_SetLuaSaveFloat (unknown)
NativeFunction makeSetLuaSaveDataFloat(const BindingContext& context) {
    return [state = context.state](std::span<const Value> args) {
        if (const std::optional<std::size_t> slot = saveSlot(intArg(args, 0))) {
            state->luaSaveFloats.at(*slot) = static_cast<float>(binding::number(args, 1));
        }
        return binding::none();
    };
}

// `SetStartGameCallback(name)`: the Lua function InitLevel calls once the level is ready (at most 31 characters kept);
// nil clears it.
// @orig 0x0036df98 SetStartGameCallback (unknown)
// @orig 0x0015fe50 InitLevel_SetStartCallback (InitLevel.cpp)
NativeFunction makeSetStartGameCallback(const BindingContext& context) {
    return [state = context.state](std::span<const Value> args) {
        state->startGameCallback = binding::string(args, 0).substr(0, kCallbackNameLength);
        return binding::none();
    };
}

// `GetRumbleModeData(data)`: copies the Rumble menu's 23 set-up values into data[1]..data[23], overwriting them.
// @orig 0x0036b9f0 GetRumbleModeData (unknown)
// @orig 0x001f26e0 RumbleMode_GetData (unknown)
NativeFunction makeGetRumbleModeData(const BindingContext& context) {
    return [state = context.state](std::span<const Value> args) -> binding::Results {
        if (args.empty() || args[0].table() == nullptr) {
            return binding::none();
        }
        Table& data = *args[0].table();
        for (std::size_t i = 0; i < state->rumble.values.size(); ++i) {
            const auto key = static_cast<double>(i + 1);
            if (auto set = data.set(Value(key), Value(static_cast<double>(state->rumble.values.at(i)))); !set) {
                return std::unexpected(set.error());
            }
        }
        return binding::none();
    };
}

// `GetRumbleModeGangName(side)`: the name of the gang Rumble side 1 chose, or side 2's for any other side; empty until
// the gang screen is confirmed.
// @orig 0x0036bac0 GetRumbleModeGangName (unknown)
// @orig 0x001f26a8 RumbleMode_GetGangName (unknown)
// @orig 0x001fe048 RumbleMode_GetGang1Name (unknown)
// @orig 0x001fe0c8 RumbleMode_GetGang2Name (unknown)
NativeFunction makeGetRumbleModeGangName(const BindingContext& context) {
    return [state = context.state](std::span<const Value> args) -> binding::Results {
        // The side is read as an unsigned integer: 1 is the first side, anything else the second.
        const bool first = std::trunc(binding::number(args, 0)) == 1.0;
        return std::vector<Value>{Value(state->rumble.gangNames[first ? 0 : 1])};
    };
}

} // namespace

void addLevelBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle) {
    vm.registerFunction("AddFlag", makeAddFlag(context, std::move(nextHandle)));
    vm.registerFunction("FindFlag", makeFindFlag(context));
    vm.registerFunction("GetFlagPos", makeGetFlagPos(context));
    vm.registerFunction("GetPosition", makeGetPosition(context));
    vm.registerFunction("TeleportToFlag", makeTeleportToFlag(context));
    vm.registerFunction("CfgSetDatabaseSizes", makeCfgSetDatabaseSizes(context));
    vm.registerFunction("GetLUASaveDataFloat", makeGetLuaSaveDataFloat(context));
    vm.registerFunction("SetLUASaveDataFloat", makeSetLuaSaveDataFloat(context));
    vm.registerFunction("SetStartGameCallback", makeSetStartGameCallback(context));
    vm.registerFunction("GetRumbleModeData", makeGetRumbleModeData(context));
    vm.registerFunction("GetRumbleModeGangName", makeGetRumbleModeGangName(context));
}

} // namespace coney::script
