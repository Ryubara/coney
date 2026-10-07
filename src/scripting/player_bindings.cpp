// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/player_bindings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/pads.h"
#include "scripting/binding_args.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"

namespace coney::script {

namespace {

// The unlockable that lets a player carry a fourth revive: type 6, data 7 (docs/research/player-state.md#unlockables).
constexpr std::uint8_t kUpgradeType = 6;
constexpr std::uint32_t kExtraReviveUpgrade = 7;

// Argument `i` truncated to a whole number, as tolua reads an integer.
int intArg(std::span<const Value> args, std::size_t i) {
    const double value = std::trunc(binding::number(args, i));
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<int>(std::clamp(value, -2147483648.0, 2147483647.0));
}

// Argument `i` as an unsigned integer, as tolua reads one: the number truncated, then its low 32 bits.
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    const double value = std::trunc(binding::number(args, i));
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(std::fmod(value, 4294967296.0)));
}

// Argument `i` as tolua reads a boolean with a default: absent is the default, nil or false (and 0) false.
bool booleanArg(std::span<const Value> args, std::size_t i, bool fallback) {
    if (i >= args.size()) {
        return fallback;
    }
    if (const std::optional<double> value = args[i].number()) {
        return *value != 0.0;
    }
    return !args[i].isNil();
}

// A player argument (1 or 2, default 1) as the inventory's 0-based index; -1 for any other value.
int playerArg(std::span<const Value> args, std::size_t i) {
    const int player = i < args.size() ? intArg(args, i) : 1;
    return player == 1 || player == 2 ? player - 1 : -1;
}

// The 0-based player whose human has `handle`; nothing for no human or an AI human.
std::optional<int> playerOfHuman(CreatedHumans* humans, double handle) {
    if (humans == nullptr) {
        return std::nullopt;
    }
    const HumanCreation* human = humans->find(std::trunc(handle));
    if (human == nullptr || (human->playerIndex != 1 && human->playerIndex != 2)) {
        return std::nullopt;
    }
    return human->playerIndex - 1;
}

// The revive limit follows the upgrade, which the profile may have unlocked since the last give.
void syncReviveLimit(GameState& state) {
    state.player.inventory.setReviveUpgrade(
        state.player.unlocks.isDataUnlocked(state.saved, kUpgradeType, kExtraReviveUpgrade));
}

// Adds `amount` dollars to `player` (0-based; -1 does nothing), then runs the money callback with (player, amount).
// @orig 0x0041ef00 GiveMoney (unknown)
void giveMoney(ScriptSystem& scripts, GameState& state, int player, int amount) {
    if (player < 0) {
        return;
    }
    state.player.inventory.give(player, item::kMoney, amount);
    if (!state.player.moneyCallback.empty()) {
        const std::vector<Value> args{Value(static_cast<double>(player)), Value(static_cast<double>(amount))};
        scripts.call(state.player.moneyCallback, args);
    }
}

} // namespace

void addInventoryItem(ScriptSystem& scripts, GameState& state, int player, int item, int amount, bool notify) {
    if (player < 0 || player >= Inventory::kPlayers || item < 0 || item >= Inventory::kItems) {
        return;
    }
    state.player.inventory.give(player, item, amount);
    // Calls `name` with `args` when it names a function.
    const auto callback = [&scripts](const std::string& name, std::span<const Value> args) {
        if (!name.empty() && scripts.hasFunction(name)) {
            scripts.call(name, args);
        }
    };
    const Value who(static_cast<double>(player));
    const Value what(static_cast<double>(item));
    if (item == item::kMoney) {
        const std::array<Value, 2> args{who, Value(static_cast<double>(amount))};
        callback(state.player.moneyCallback, args);
    }
    if (notify) {
        const std::array<Value, 1> itemOnly{what};
        callback(state.player.pickupCallback, itemOnly);
        const std::array<Value, 2> both{who, what};
        callback(state.player.huInventoryCallback, both);
    }
}

namespace {

// Registers one binding that reads the state through `state`.
template <typename Body> void add(LuaVm& vm, std::string_view name, Body body) {
    vm.registerFunction(name, NativeFunction(std::move(body)));
}

// The inventory bindings (docs/references/bindings/level.md#invgetmoney and the ones after it).
void addInventoryBindings(ScriptSystem& scripts, LuaVm& vm, GameState& state) {
    Inventory& inventory = state.player.inventory;
    // `CfgInventoryItem(object, id, count, sound, ms)`: one slot's configuration, for both players.
    add(vm, "CfgInventoryItem", [&inventory](std::span<const Value> args) {
        const int ms = args.size() > 4 ? intArg(args, 4) : 10000;
        inventory.configure(intArg(args, 1), binding::string(args, 0), intArg(args, 2), binding::string(args, 3), ms);
        return binding::none();
    });
    // `CfgInventoryCallback(fn)`: the pickup callback's name.
    // @orig 0x0041ece8 Cfg_SetInventoryCallback (unknown)
    add(vm, "CfgInventoryCallback", [&state](std::span<const Value> args) {
        state.player.pickupCallback = binding::string(args, 0);
        return binding::none();
    });
    // `CfgHuInventoryCallback(fn)`: the second pickup callback's name, called with (player, item).
    // @orig 0x0041ed10 Cfg_SetHuInventoryCallback (unknown)
    add(vm, "CfgHuInventoryCallback", [&state](std::span<const Value> args) {
        state.player.huInventoryCallback = binding::string(args, 0);
        return binding::none();
    });
    // `CfgMoneyCallback(fn)`: the money-changed callback's name, called with (player, amount).
    // @orig 0x0041ed60 Cfg_SetMoneyCallback (unknown)
    add(vm, "CfgMoneyCallback", [&state](std::span<const Value> args) {
        state.player.moneyCallback = binding::string(args, 0);
        return binding::none();
    });
    add(vm, "GiveMoney", [&scripts, &state](std::span<const Value> args) {
        giveMoney(scripts, state, playerArg(args, 1), intArg(args, 0));
        return binding::none();
    });
    // `TakeMoney(amount, player)`: GiveMoney with the amount negated.
    // @orig 0x0041ef60 TakeMoney (unknown)
    add(vm, "TakeMoney", [&scripts, &state](std::span<const Value> args) {
        giveMoney(scripts, state, playerArg(args, 1), -intArg(args, 0));
        return binding::none();
    });
    // @orig 0x0041ee88 InvGetMoney (unknown)
    add(vm, "InvGetMoney", [&inventory](std::span<const Value> args) {
        return binding::number(inventory.count(playerArg(args, 0), item::kMoney));
    });
    add(vm, "InvSetMoney", [&inventory](std::span<const Value> args) {
        inventory.setMoney(playerArg(args, 1), intArg(args, 0));
        return binding::none();
    });
    // @orig 0x0041f000 InvGetSpraycanCharges (unknown)
    add(vm, "InvGetSpraycanCharges", [&inventory](std::span<const Value> args) {
        return binding::number(inventory.count(playerArg(args, 0), item::kSprayPaint));
    });
    add(vm, "InvSetSpraycanCharges", [&inventory](std::span<const Value> args) {
        inventory.setSprayPaint(playerArg(args, 1), intArg(args, 0));
        return binding::none();
    });
    add(vm, "InvGiveItem", [&state](std::span<const Value> args) {
        syncReviveLimit(state);
        state.player.inventory.give(playerArg(args, 2), intArg(args, 0), intArg(args, 1));
        return binding::none();
    });
    // `InvGiveRevive(count, player)`: revives, at most 3 (4 with the upgrade).
    // @orig 0x0041edb8 InvGiveRevive (unknown)
    add(vm, "InvGiveRevive", [&state](std::span<const Value> args) {
        syncReviveLimit(state);
        state.player.inventory.give(playerArg(args, 1), item::kRevive, intArg(args, 0));
        return binding::none();
    });
    // @orig 0x0041ee20 InvGiveSkeletonKey (unknown)
    add(vm, "InvGiveSkeletonKey", [&inventory](std::span<const Value> args) {
        inventory.give(playerArg(args, 1), item::kHandcuffKey, intArg(args, 0));
        return binding::none();
    });
    // @orig 0x0041efd0 InvNumberOf (unknown)
    add(vm, "InvNumberOf", [&inventory](std::span<const Value> args) {
        return binding::number(inventory.count(playerArg(args, 1), intArg(args, 0)));
    });
    // @orig 0x0041edf0 InvNumberRevives (unknown)
    add(vm, "InvNumberRevives", [&inventory](std::span<const Value> args) {
        return binding::number(inventory.count(playerArg(args, 0), item::kRevive));
    });
    // @orig 0x0041ee58 InvNumberSkeletonKeys (unknown)
    add(vm, "InvNumberSkeletonKeys", [&inventory](std::span<const Value> args) {
        return binding::number(inventory.count(playerArg(args, 0), item::kHandcuffKey));
    });
    add(vm, "InvPlayerHasItem", [&inventory](std::span<const Value> args) {
        return binding::boolean(inventory.has(playerArg(args, 1), intArg(args, 0)));
    });
}

// The statistics bindings (docs/references/bindings/level.md#statadd and the ones after it).
void addStatBindings(LuaVm& vm, const BindingContext& context) {
    GameState& state = *context.state;
    PlayerStats& stats = state.player.stats;
    add(vm, "CfgSetStatValue", [&stats](std::span<const Value> args) {
        stats.setPoints(unsignedArg(args, 0), unsignedArg(args, 1), static_cast<std::uint16_t>(unsignedArg(args, 2)));
        return binding::none();
    });
    // `CfgSetStatTypeMax(combat, crime, harmony, style, mission, bonus)`: the six maxima at `0x00715840`.
    add(vm, "CfgSetStatTypeMax", [&stats](std::span<const Value> args) {
        constexpr std::array<StatCategory, 6> kOrder{StatCategory::Combat, StatCategory::Crime,   StatCategory::Harmony,
                                                     StatCategory::Style,  StatCategory::Mission, StatCategory::Bonus};
        for (std::size_t i = 0; i < kOrder.size(); ++i) {
            stats.setMaximum(kOrder.at(i), unsignedArg(args, i));
        }
        return binding::none();
    });
    add(vm, "StatAdd", [humans = context.humans, &stats](std::span<const Value> args) {
        if (const std::optional<int> player = playerOfHuman(humans, binding::number(args, 0))) {
            const std::uint32_t amount = args.size() > 3 ? unsignedArg(args, 3) : 1;
            stats.add(*player, unsignedArg(args, 1), unsignedArg(args, 2) & 0xffffU, amount);
        }
        return binding::none();
    });
    add(vm, "StatGetScore", [humans = context.humans, &stats](std::span<const Value> args) {
        const std::optional<int> player = playerOfHuman(humans, binding::number(args, 0));
        return binding::number(player.has_value() ? stats.score(*player) : 0.0);
    });
    add(vm, "StatResetPlayer", [humans = context.humans, &stats](std::span<const Value> args) {
        if (const std::optional<int> player = playerOfHuman(humans, binding::number(args, 0))) {
            stats.resetPlayer(*player);
        }
        return binding::none();
    });
    add(vm, "StatReset", [&stats](std::span<const Value>) {
        stats.reset();
        return binding::none();
    });
}

// The unlockables' bindings (docs/references/bindings/level.md#um_unlock and its neighbours).
void addUnlockBindings(LuaVm& vm, GameState& state) {
    UnlockRecords& unlocks = state.player.unlocks;
    add(vm, "UM_SetNumUnlockables", [&unlocks](std::span<const Value> args) {
        unlocks.setCount(unsignedArg(args, 0));
        return binding::none();
    });
    add(vm, "UM_SetUnlockable", [&unlocks](std::span<const Value> args) {
        const int index = intArg(args, 0);
        if (index >= 0) {
            unlocks.set(static_cast<std::size_t>(index),
                        UnlockRecord{static_cast<std::uint8_t>(unsignedArg(args, 1)),
                                     static_cast<std::uint8_t>(unsignedArg(args, 2)),
                                     static_cast<std::uint8_t>(unsignedArg(args, 3)),
                                     static_cast<std::uint8_t>(unsignedArg(args, 4)),
                                     static_cast<std::uint16_t>(unsignedArg(args, 5)), unsignedArg(args, 6)});
        }
        return binding::none();
    });
    add(vm, "UM_Reset", [&unlocks](std::span<const Value>) {
        unlocks.reset();
        return binding::none();
    });
    add(vm, "UM_Unlock", [&state, &unlocks](std::span<const Value> args) {
        unlocks.unlock(state.saved, static_cast<std::uint8_t>(unsignedArg(args, 0)),
                       static_cast<std::uint8_t>(unsignedArg(args, 1)),
                       static_cast<std::uint8_t>(unsignedArg(args, 2)));
        return binding::none();
    });
    add(vm, "UM_IsLevelComplete", [&state, &unlocks](std::span<const Value> args) {
        return binding::boolean(unlocks.isLevelComplete(state.saved, static_cast<std::uint8_t>(unsignedArg(args, 0))));
    });
    // @orig 0x00423868 UM_IsDataUnlocked (unknown)
    add(vm, "UM_IsDataUnlocked", [&state, &unlocks](std::span<const Value> args) {
        return binding::boolean(
            unlocks.isDataUnlocked(state.saved, static_cast<std::uint8_t>(unsignedArg(args, 0)), unsignedArg(args, 1)));
    });
    add(vm, "UM_IsTypeDirty", [&state, &unlocks](std::span<const Value> args) {
        return binding::boolean(unlocks.isTypeDirty(state.saved, static_cast<std::uint8_t>(unsignedArg(args, 0)),
                                                    booleanArg(args, 1, true)));
    });
    add(vm, "UM_IsDataDirty", [&state, &unlocks](std::span<const Value> args) {
        return binding::boolean(unlocks.isDataDirty(state.saved, static_cast<std::uint8_t>(unsignedArg(args, 0)),
                                                    unsignedArg(args, 1), booleanArg(args, 2, true)));
    });
    // `UM_GetUnlockablesByType(type, out)`: out[1..32] become the indices of the records of the type, in table order,
    // then 65535. **Coney choice**: past 32 matches the rest are dropped (the original overruns its buffer).
    // @orig 0x004237a8 UM_GetUnlockablesByType (unknown)
    // @orig 0x00423eb0 Unlockables_ListByType (unknown)
    add(vm, "UM_GetUnlockablesByType", [&unlocks](std::span<const Value> args) -> binding::Results {
        constexpr std::size_t kSlots = 32;
        constexpr double kUnused = 65535.0;
        if (args.size() < 2 || args[1].table() == nullptr) {
            return binding::none();
        }
        const auto type = static_cast<std::uint8_t>(unsignedArg(args, 0) & 0xffU);
        std::vector<double> indices;
        for (std::size_t index = 0; index < unlocks.records().size() && indices.size() < kSlots; ++index) {
            if (unlocks.records()[index].type == type) {
                indices.push_back(static_cast<double>(index));
            }
        }
        indices.resize(kSlots, kUnused);
        Table& out = *args[1].table();
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            if (auto set = out.set(Value(static_cast<double>(slot + 1)), Value(indices[slot])); !set) {
                return std::unexpected(set.error());
            }
        }
        return binding::none();
    });
    // `UM_GetRecordData(index, field)`: 0 level, 1 group, 2 item, 3 type, 4 extra, 5 data; 0 for a bad index or field.
    // @orig 0x004238e8 UM_GetRecordData (unknown)
    add(vm, "UM_GetRecordData", [&unlocks](std::span<const Value> args) {
        const int index = intArg(args, 0);
        const UnlockRecord* record = index >= 0 ? unlocks.record(static_cast<std::size_t>(index)) : nullptr;
        if (record == nullptr) {
            return binding::number(0.0);
        }
        switch (intArg(args, 1)) {
        case 0:
            return binding::number(record->level);
        case 1:
            return binding::number(record->group);
        case 2:
            return binding::number(record->item);
        case 3:
            return binding::number(record->type);
        case 4:
            return binding::number(record->extra);
        case 5:
            return binding::number(record->data);
        default:
            return binding::number(0.0);
        }
    });
}

// The stopwatch, crime, pad, store and join bindings.
void addWorldBindings(ScriptSystem& scripts, LuaVm& vm, GameState& state) {
    PlayerState& player = state.player;
    add(vm, "W_SetStopWatch", [&player](std::span<const Value> args) {
        player.stopWatch.set(intArg(args, 0), intArg(args, 1), binding::string(args, 2));
        return binding::none();
    });
    add(vm, "W_StartStopWatch", [&scripts, &player](std::span<const Value> args) {
        player.stopWatch.start(booleanArg(args, 0, false), scripts.now());
        return binding::none();
    });
    // @orig 0x00423638 W_GetStopWatchTime (unknown)
    add(vm, "W_GetStopWatchTime",
        [&player](std::span<const Value>) { return binding::number(player.stopWatch.time()); });
    add(vm, "ReportCrime", [&player](std::span<const Value> args) {
        player.crimes.setReporting(booleanArg(args, 0, false));
        return binding::none();
    });
    // `CfgSetSteroTheftHandler(fn)`: the stereo theft's callback.
    // @orig 0x00236508 Cfg_SetStereoTheftHandler (unknown)
    add(vm, "CfgSetSteroTheftHandler", [&player](std::span<const Value> args) {
        player.stereoTheftHandler = binding::string(args, 0);
        return binding::none();
    });
    // `CfgMultiplayerJoin(on)`. Turning it on also resets the join state through `0x0041a460(-1)`, which is not traced,
    // so Coney keeps the flag only.
    // @orig 0x0041da08 Cfg_SetMultiplayerJoin (unknown)
    add(vm, "CfgMultiplayerJoin", [&player](std::span<const Value> args) {
        player.multiplayerJoin = booleanArg(args, 0, false);
        return binding::none();
    });
    // `SetMultiplayerCallback(fn)`: the two-player sync's function; nil clears it.
    // @orig 0x0041b0f0 GameState_SetMultiplayerCallback (unknown)
    add(vm, "SetMultiplayerCallback", [&player](std::span<const Value> args) {
        player.multiplayerCallback = !args.empty() && !args[0].isNil() ? binding::string(args, 0) : std::string();
        return binding::none();
    });
    // `PadSetHandler(player, button, callback)`: player 0 is port 1's record, any other port 2's.
    add(vm, "PadSetHandler", [&player](std::span<const Value> args) {
        const std::size_t port = unsignedArg(args, 0) == 0 ? 0 : 1;
        player.pads.set(Pads::recordOfPort(port), static_cast<std::uint16_t>(unsignedArg(args, 1) & 0xffffU),
                        binding::string(args, 2));
        return binding::none();
    });
    add(vm, "PadSetHandlerEx", [&player](std::span<const Value> args) {
        player.pads.setTargetHandler(binding::string(args, 0));
        return binding::none();
    });
}

} // namespace

void addPlayerBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context) {
    GameState& state = *context.state;
    addInventoryBindings(scripts, vm, state);
    addStatBindings(vm, context);
    addUnlockBindings(vm, state);
    addWorldBindings(scripts, vm, state);
}

} // namespace coney::script
