// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/rumble_bindings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "gui/rumble_mode_gui/rumble_data.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// How many modes `CfgRumbleArena`'s table holds.
constexpr std::size_t kArenaModes = 16;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as tolua reads an unsigned integer: truncated, then kept to 32 bits.
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(wholeArg(args, i));
}

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].isNil()) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// Element `k` (1-based) of the table argument `i`, truncated; 0 when the argument is not a table or the element is
// missing, as tolua reads it.
std::int64_t tableElement(std::span<const Value> args, std::size_t i, std::size_t k) {
    if (i >= args.size() || args[i].table() == nullptr) {
        return 0;
    }
    const Value element = args[i].table()->get(Value(static_cast<double>(k)));
    const std::array<Value, 1> one{element};
    return wholeArg(one, 0);
}

// `CfgRumbleGame(title, mode, onePlayer, coop, versus, unused, size, preset1, preset2, description)`.
// @orig 0x0036b4f0 CfgRumbleGame (unknown)
NativeFunction makeCfgRumbleGame(const BindingContext& context) {
    return [state = context.state, data = context.rumble](std::span<const Value> args) {
        if (data != nullptr) {
            gui::RumbleModeEntry entry;
            entry.title = binding::string(args, 0);
            entry.mode = static_cast<std::uint16_t>(unsignedArg(args, 1));
            entry.playerOptions = {boolArg(args, 2), boolArg(args, 3), boolArg(args, 4)};
            // Argument 6 is never stored.
            entry.gangSize = static_cast<std::uint16_t>(unsignedArg(args, 6));
            entry.presets = {static_cast<std::uint16_t>(unsignedArg(args, 7)),
                             static_cast<std::uint16_t>(unsignedArg(args, 8))};
            entry.description = binding::string(args, 9);
            gui::addRumbleMode(*data, state->unlockables, std::move(entry));
        }
        return binding::none();
    };
}

// `CfgRumbleGang(gang, name, members)`: the gang id read as a signed integer, nine types from members[1..9].
// @orig 0x0036b790 CfgRumbleGang (unknown)
NativeFunction makeCfgRumbleGang(const BindingContext& context) {
    return [state = context.state, data = context.rumble](std::span<const Value> args) {
        if (data != nullptr) {
            gui::RumbleGangEntry::Roster members{};
            for (std::size_t k = 0; k < members.size(); ++k) {
                members.at(k) = static_cast<std::uint32_t>(tableElement(args, 2, k + 1));
            }
            gui::addRumbleGang(*data, state->unlockables, static_cast<std::int32_t>(wholeArg(args, 0)),
                               binding::string(args, 1), members);
        }
        return binding::none();
    };
}

// `CfgRumbleArena(levelId, value, modes)`: modes[1..16].
// @orig 0x0036b670 CfgRumbleArena (unknown)
NativeFunction makeCfgRumbleArena(const BindingContext& context) {
    return [state = context.state, data = context.rumble](std::span<const Value> args) {
        if (data != nullptr) {
            std::array<std::int64_t, kArenaModes> modes{};
            for (std::size_t k = 0; k < modes.size(); ++k) {
                modes.at(k) = tableElement(args, 2, k + 1);
            }
            gui::addRumbleArena(*data, *state, static_cast<int>(unsignedArg(args, 0)), unsignedArg(args, 1), modes);
        }
        return binding::none();
    };
}

// `CfgRumbleChar(type, name, gangName, gang, rank, n6, n7, bio)`.
// @orig 0x0036b8a8 CfgRumbleChar (unknown)
NativeFunction makeCfgRumbleChar(const BindingContext& context) {
    return [data = context.rumble](std::span<const Value> args) {
        if (data != nullptr) {
            gui::RumbleCharEntry entry;
            entry.name = binding::string(args, 1);
            entry.gangName = binding::string(args, 2);
            entry.gang = static_cast<std::uint8_t>(unsignedArg(args, 3));
            entry.rank = static_cast<std::uint8_t>(unsignedArg(args, 4));
            entry.n6 = static_cast<std::uint16_t>(unsignedArg(args, 5));
            entry.n7 = static_cast<std::uint8_t>(unsignedArg(args, 6));
            entry.bio = binding::string(args, 7);
            gui::addRumbleChar(*data, unsignedArg(args, 0), std::move(entry));
        }
        return binding::none();
    };
}

} // namespace

void addRumbleBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("CfgRumbleArena", makeCfgRumbleArena(context));
    vm.registerFunction("CfgRumbleChar", makeCfgRumbleChar(context));
    vm.registerFunction("CfgRumbleGame", makeCfgRumbleGame(context));
    vm.registerFunction("CfgRumbleGang", makeCfgRumbleGang(context));
}

} // namespace coney::script
