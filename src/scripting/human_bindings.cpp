// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/human_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "combat/combat_tuning.h"
#include "human/human_flags.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// Coney's NilHandle (scripting/script_bindings.cpp).
constexpr double kNilHandle = 0.0;
// The longest names the original keeps: a carried item's (`+0x257`) and a rage handler's (32-byte buffers).
constexpr std::size_t kNameLength = 31;
// The command ids `EnableCommand` takes, and the Warrior commands `WCIssueCommand` takes.
constexpr int kLastCommand = 57;
constexpr int kWarriorCommandCount = static_cast<int>(kWarriorCommands);
// Money is clamped to this (`HuSetMoney`).
constexpr int kMaxMoney = 999;
// `SetInterrogateParam`'s sets: 0-2 write the first override, 3-5 the second.
constexpr std::uint32_t kInterrogateSetsPerOverride = 3;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as an integer.
int intArg(std::span<const Value> args, std::size_t i) { return static_cast<int>(wholeArg(args, i)); }

// Argument `i` as an unsigned integer.
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(wholeArg(args, i));
}

// Argument `i` as a float.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Whether argument `i` is missing or nil, so a default applies.
bool absent(std::span<const Value> args, std::size_t i) { return i >= args.size() || args[i].isNil(); }

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// Argument `i` as a boolean that is `fallback` when omitted; a nil passed (Lua 4's false) is false, as tolua reads it.
bool boolArgOr(std::span<const Value> args, std::size_t i, bool fallback) {
    return i >= args.size() ? fallback : boolArg(args, i);
}

// Argument `i` as `HuSetGodMode`'s and `HuSetDemiGodMode`'s switch: only exactly 1 (Lua 4's true) sets the bit.
bool exactlyOne(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return false;
    }
    return args[i].type() == Value::Type::Number ? binding::number(args, i) == 1.0 : boolArg(args, i);
}

// A handle argument: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(wholeArg(args, i)));
}

// A string argument that is empty for nil (a callback name), cut to `length` characters.
std::string nameArg(std::span<const Value> args, std::size_t i, std::size_t length = std::string::npos) {
    std::string name = absent(args, i) ? std::string{} : binding::string(args, i);
    if (name.size() > length) {
        name.resize(length);
    }
    return name;
}

// Numbers 1..n of the table at argument `i`, 0 for a missing one; all 0 when it is not a table.
template <std::size_t N> std::array<double, N> tableArg(std::span<const Value> args, std::size_t i) {
    std::array<double, N> values{};
    if (i >= args.size() || args[i].table() == nullptr) {
        return values;
    }
    const Table& table = *args[i].table();
    for (std::size_t k = 0; k < N; ++k) {
        values.at(k) = table.get(Value(static_cast<double>(k + 1))).number().value_or(0.0);
    }
    return values;
}

// The level's humans, if there is a level with an AI host.
HumanBindingHost* hostOf(const BindingContext& context) {
    return context.ai != nullptr ? context.ai->humans() : nullptr;
}

// A binding that hands its arguments to the host when there is one and returns nothing. The host is read at each call,
// not at registration: a level gives its brains to a Lua state made before it (gamemodes/gameplay_mode.h).
template <typename Body> NativeFunction hostCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (HumanBindingHost* host = hostOf(*context); host != nullptr) {
            body(*host, args);
        }
        return binding::none();
    };
}

// A configuration binding: changes the game state's rules with `body`, then tells the level's host, if any.
template <typename Body> NativeFunction rulesCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        CharacterRules& rules = context->state->characters;
        body(rules, args);
        if (HumanBindingHost* host = hostOf(*context); host != nullptr) {
            host->applyRules(rules);
        }
        return binding::none();
    };
}

// The status of the human named by argument 0; nothing without a host or such a human.
std::optional<HumanStatus> statusArg(const BindingContext& context, std::span<const Value> args) {
    const HumanBindingHost* host = hostOf(context);
    return host != nullptr ? host->status(handleArg(args, 0)) : std::nullopt;
}

// A getter of the human named by argument 0: `read` of its status, `fallback` for a missing human.
template <typename Read> NativeFunction statusGetter(const BindingContext& context, Read read) {
    return [context = &context, read](std::span<const Value> args) { return read(statusArg(*context, args)); };
}

// The flag setters: each sets or clears its bits of the human flag word (human/human_flags.h); `inverted` writes the
// opposite (`HuSetPreventRage` clears the rage-allowed bit to prevent rage).
struct FlagSetter {
    std::string_view name;
    std::uint64_t bits;
    bool inverted = false;
};
// @orig 0x00235b68 Human_SetFastClimber (unknown)
// @orig 0x00235bc8 Human_SetUngrabbable (unknown)
// @orig 0x00235c28 Human_SetUngroundable (unknown)
// @orig 0x00235c88 Human_SetUnstunnable (unknown)
// @orig 0x00235d50 Human_SetReducedReact (unknown)
// @orig 0x00235ce8 Human_SetIncreasedReact (unknown)
// @orig 0x00237328 Human_SetKeepWeapon (unknown)
// @orig 0x00236ad0 Human_SetPreventRage (unknown)
// @orig 0x002350c8 Human_SetTireless (unknown)
// @orig 0x00237468 Human_SetNoThrowWeapon (unknown)
// @orig 0x00234038 Human_SetNoTarget (unknown)
constexpr std::array kFlagSetters{
    FlagSetter{.name = "HuSetFastClimber", .bits = human::flag::kFastClimber, .inverted = false},
    FlagSetter{.name = "HuSetUngrabbable", .bits = human::flag::kUngrabbable, .inverted = false},
    FlagSetter{.name = "HuSetUngroundable", .bits = human::flag::kUngroundable, .inverted = false},
    FlagSetter{.name = "HuSetUnstunnable", .bits = human::flag::kUnstunnable, .inverted = false},
    FlagSetter{.name = "HuSetReducedReact", .bits = human::flag::kReducedReact, .inverted = false},
    FlagSetter{.name = "HuSetIncreasedReact", .bits = human::flag::kIncreasedReact, .inverted = false},
    FlagSetter{.name = "HuSetKeepWeapon", .bits = human::flag::kKeepWeapon, .inverted = false},
    FlagSetter{.name = "HuSetPreventRage", .bits = human::flag::kRageAllowed, .inverted = true},
    FlagSetter{.name = "HuSetTireless", .bits = human::flag::kTireless, .inverted = false},
    FlagSetter{.name = "HuSetNoThrowWeapon", .bits = human::flag::kNoThrowWeapon, .inverted = false},
    FlagSetter{.name = "HuSetNoTarget", .bits = human::flag::kNoTarget, .inverted = false},
};

// ---- The humans.

// `HuSetGodMode(human, on)`.
// @orig 0x00235a28 Human_SetGodMode (unknown)
NativeFunction makeSetGodMode(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setFlags(handleArg(args, 0), human::flag::kGod, exactlyOne(args, 1));
    });
}

// `HuSetDemiGodMode(human, on, fraction)`: turning it on stores the fraction in the one global every human shares.
// @orig 0x002359a0 Human_SetDemiGodMode (unknown)
NativeFunction makeSetDemiGodMode(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const bool on = exactlyOne(args, 1);
        if (on) {
            combat::combatTuning().healthFloor = floatArg(args, 2);
        }
        if (HumanBindingHost* host = hostOf(*context); host != nullptr) {
            host->setFlags(handleArg(args, 0), human::flag::kDemiGod, on);
        }
        return binding::none();
    };
}

// `HuSetLockedRage(human, on)`.
// @orig 0x00236b38 Human_SetLockedRage (unknown)
NativeFunction makeSetLockedRage(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setLockedRage(handleArg(args, 0), boolArg(args, 1));
    });
}

// `HuSetFullRage(human)`: held for the rage handlers' hold time.
// @orig 0x00236a68 Human_SetFullRage (unknown)
NativeFunction makeSetFullRage(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (HumanBindingHost* host = hostOf(*context); host != nullptr) {
            host->fillRage(handleArg(args, 0), static_cast<int>(context->state->characters.rage.holdMs));
        }
        return binding::none();
    };
}

// `HuSetRageFrac(human, fraction)`.
// @orig 0x002369b8 Human_SetRageFrac (unknown)
NativeFunction makeSetRageFrac(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setRageFraction(handleArg(args, 0), floatArg(args, 1));
    });
}

// `HuSetHealthPercent(human, percent)`.
// @orig 0x002378a8 Human_SetHealthPercent (unknown)
NativeFunction makeSetHealthPercent(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setHealthPercent(handleArg(args, 0), floatArg(args, 1));
    });
}

// `HuRevive(human)`.
// @orig 0x002377f8 Human_Revive (unknown)
NativeFunction makeRevive(const BindingContext& context) {
    return hostCall(context,
                    [](HumanBindingHost& host, std::span<const Value> args) { host.revive(handleArg(args, 0)); });
}

// `HuSetNormalMode(human, full)`: full defaults to true.
// @orig 0x0023a210 Human_SetNormalMode (unknown)
NativeFunction makeSetNormalMode(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setNormalMode(handleArg(args, 0), boolArgOr(args, 1, true));
    });
}

// `HuSetArrested(human, arrested)`.
// @orig 0x00237700 Human_SetArrested (unknown)
NativeFunction makeSetArrested(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setArrested(handleArg(args, 0), boolArg(args, 1));
    });
}

// `HuSetPushable(human, on)`.
// @orig 0x00235268 Human_SetPushable (unknown)
NativeFunction makeSetPushable(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setPushable(handleArg(args, 0), boolArg(args, 1));
    });
}

// `HuSetMoney(human, dollars)`: clamped to 0-999.
// @orig 0x00238100 Human_SetMoney (unknown)
NativeFunction makeSetMoney(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setMoney(handleArg(args, 0), std::clamp(intArg(args, 1), 0, kMaxMoney));
    });
}

// `HuSetCarriedItem(human, object)`: 31 characters kept.
// @orig 0x00238230 Human_SetCarriedItemName (unknown)
NativeFunction makeSetCarriedItem(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setCarriedItem(handleArg(args, 0), nameArg(args, 1, kNameLength));
    });
}

// `HuSetMugCallback(human, callback)`: nil removes it.
// @orig 0x00239e78 Human_SetMugCallback (unknown)
NativeFunction makeSetMugCallback(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setMugCallback(handleArg(args, 0), nameArg(args, 1));
    });
}

// `HuSetLookTarget(human, target, ms, weight, flagA, flagB)`: ms 2000 and weight 1 when omitted.
// @orig 0x0023a3b8 Human_SetLookTarget (unknown)
NativeFunction makeSetLookTarget(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        constexpr std::uint32_t kOptionA = 0x20;
        constexpr std::uint32_t kOptionB = 0x40;
        // The time is read as an unsigned integer: the scripts' -1 is the largest, no limit.
        const std::int64_t time = absent(args, 2) ? 2000 : wholeArg(args, 2);
        host.setLookTarget(
            LookTargetCall{.human = handleArg(args, 0),
                           .target = handleArg(args, 1),
                           .timeMs = time < 0 ? -1 : time,
                           .weight = absent(args, 3) ? 1.0F : floatArg(args, 3),
                           .options = (boolArg(args, 4) ? kOptionA : 0U) | (boolArg(args, 5) ? kOptionB : 0U)});
    });
}

// `HuTeleportNearHuman(human, near)`: nothing when both are the same.
// @orig 0x0023b138 Human_TeleportNear (unknown)
NativeFunction makeTeleportNear(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        const double human = handleArg(args, 0);
        const double near = handleArg(args, 1);
        if (human != near) {
            host.teleportNear(human, near);
        }
    });
}

// `HuLockPad(human, lock)`.
// @orig 0x0023ae40 Human_LockPad (unknown)
NativeFunction makeLockPad(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.lockPad(handleArg(args, 0), boolArg(args, 1));
    });
}

// `EnableCommand(human, command, enable)`: a command outside 1-57 is ignored.
// @orig 0x001467a8 PlayerCommand_EnableForHuman (unknown)
NativeFunction makeEnableCommand(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        const int command = intArg(args, 1);
        if (command >= 1 && command <= kLastCommand) {
            host.enableCommand(handleArg(args, 0), command, intArg(args, 2) != 0);
        }
    });
}

// `EnableCommands(human, enable)`: every command.
// @orig 0x001463e0 PlayerCommand_EnableAllForHuman (unknown)
NativeFunction makeEnableCommands(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.enableCommand(handleArg(args, 0), 0, intArg(args, 1) != 0);
    });
}

// `HuAttachSpinningIcon(human, object, param)`. **Coney choice**: dyn_p_one and dyn_p_two, which the original swaps for
// the language's own icons, are kept as named (Coney plays the English disc).
// @orig 0x00238a88 Human_AttachSpinningIcon (unknown)
NativeFunction makeAttachSpinningIcon(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        const std::string object = nameArg(args, 1);
        if (!object.empty()) {
            host.setIcon(handleArg(args, 0), object, intArg(args, 2));
        }
    });
}

// `HuRemoveSpinningIcon(human)`.
// @orig 0x00238ae0 Human_RemoveSpinningIcon (unknown)
NativeFunction makeRemoveSpinningIcon(const BindingContext& context) {
    return hostCall(
        context, [](HumanBindingHost& host, std::span<const Value> args) { host.setIcon(handleArg(args, 0), {}, 0); });
}

// `HuDropWeapon(human)`.
// @orig 0x002994b8 Human_DropWeapon (unknown)
NativeFunction makeDropWeapon(const BindingContext& context) {
    return hostCall(context,
                    [](HumanBindingHost& host, std::span<const Value> args) { host.dropWeapon(handleArg(args, 0)); });
}

// `HuPlaceItemInHand(human, object)`: the new object's handle, or NilHandle when the human is missing or already holds
// something.
// @orig 0x00238540 Human_PlaceItemInHand (unknown)
NativeFunction makePlaceItemInHand(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        HumanBindingHost* host = hostOf(*context);
        const std::string object = nameArg(args, 1);
        if (host == nullptr || object.empty()) {
            return binding::number(kNilHandle);
        }
        return binding::number(host->placeItemInHand(handleArg(args, 0), object, nextHandle));
    };
}

// `HuUseAnim(human, slot, anim)`: slots 0-3; true when the clip, asked for with SetDynamicAnimation, is in place.
// @orig 0x00238690 Human_UseAnim (unknown)
NativeFunction makeUseAnim(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        HumanBindingHost* host = hostOf(*context);
        const int slot = intArg(args, 1);
        if (host == nullptr || slot < 0 || slot > 3) {
            return binding::boolean(false);
        }
        const std::string anim = nameArg(args, 2);
        const std::vector<std::string>& loaded = context->state->characters.dynamicAnimations;
        const bool requested = anim.empty() || std::ranges::find(loaded, anim) != loaded.end();
        return binding::boolean(host->useAnim(handleArg(args, 0), slot, anim, requested));
    };
}

// `SetDynamicAnimation(anim, release)`: adds the file to the level's list (64 entries) or, releasing, removes it.
// **Coney choice**: a full list replaces its last entry (the original replaces the largest loaded one; Coney keeps no
// sizes).
// @orig 0x0040cd78 ResourceManager_SetDynamicAnimation (unknown)
NativeFunction makeSetDynamicAnimation(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        std::vector<std::string>& list = context->state->characters.dynamicAnimations;
        const std::string anim = nameArg(args, 0);
        const auto found = std::ranges::find(list, anim);
        if (anim.empty()) {
            return binding::none();
        }
        if (boolArg(args, 1)) {
            if (found != list.end()) {
                list.erase(found);
            }
        } else if (found == list.end()) {
            if (list.size() >= kDynamicAnimations) {
                list.back() = anim;
            } else {
                list.push_back(anim);
            }
        }
        return binding::none();
    };
}

// `HuChangePlayerGang(gang, stamp)`: always true.
// @orig 0x00239b80 Players_ChangeGang (unknown)
NativeFunction makeChangePlayerGang(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (HumanBindingHost* host = hostOf(*context); host != nullptr) {
            host->changePlayerGang(intArg(args, 0), boolArg(args, 1));
        }
        return binding::boolean(true);
    };
}

// `WCEnableAllCommands(on)`: every Warrior command of every player.
// @orig 0x0041db08 GameState_EnableAllWarriorCommands (unknown)
NativeFunction makeWcEnableAllCommands(const BindingContext& context) {
    return rulesCall(context, [](CharacterRules& rules, std::span<const Value> args) {
        const bool on = boolArg(args, 0);
        for (auto& player : rules.warriorCommands) {
            player.fill(on);
        }
    });
}

// `WCIssueCommand(player, command, on)`: nothing while the commands are locked, for a human no player controls, or
// for a command the player has disabled; else it is the player's last command and the crew takes it. `on` is the
// dispatcher's forced flag (the scripts always pass true).
// @orig 0x0041dc80 GameState_IssueWarriorCommandFor (unknown)
NativeFunction makeWcIssueCommand(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        HumanBindingHost* host = hostOf(*context);
        CharacterRules& rules = context->state->characters;
        const int command = intArg(args, 1);
        if (host == nullptr || rules.warriorCommandsLocked || command < 0 || command >= kWarriorCommandCount) {
            return binding::none();
        }
        const double player = handleArg(args, 0);
        const std::optional<int> index = host->playerIndex(player);
        if (!index || *index < 0 || *index >= static_cast<int>(kWarriorPlayers)) {
            return binding::none();
        }
        const auto p = static_cast<std::size_t>(*index);
        if (!rules.warriorCommands.at(p).at(static_cast<std::size_t>(command))) {
            return binding::none();
        }
        rules.lastWarriorCommand.at(p) = command;
        host->issueWarriorCommand(player, command);
        return binding::none();
    };
}

// ---- The brains.

// `BrClearBackoff(human)`.
// @orig 0x00292cf0 Brain_ClearBackoff (unknown)
NativeFunction makeClearBackoff(const BindingContext& context) {
    return hostCall(context,
                    [](HumanBindingHost& host, std::span<const Value> args) { host.clearBackoff(handleArg(args, 0)); });
}

// `BrSetThugWantsWeapon(human, wants)`.
// @orig 0x00292848 Brain_SetWantsWeapon (unknown)
NativeFunction makeSetThugWantsWeapon(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setWantsWeapon(handleArg(args, 0), boolArg(args, 1));
    });
}

// `GoalBackoff(human, from, distance, timeMs, option)`: time -1 and option true when omitted.
// @orig 0x002d9238 Goal_Backoff (unknown)
NativeFunction makeGoalBackoff(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.goalBackoff(BackoffCall{.human = handleArg(args, 0),
                                     .from = handleArg(args, 1),
                                     .distance = floatArg(args, 2),
                                     .timeMs = absent(args, 3) ? -1 : intArg(args, 3),
                                     .option = boolArgOr(args, 4, true)});
    });
}

// `GoalBumLogic(human, bumType, option, chance, value, callback, option2)`: option true, value 3 and option2 true when
// omitted; the chance clamped to 100.
// @orig 0x002abd38 Goal_Bum (unknown)
NativeFunction makeGoalBumLogic(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        constexpr std::uint32_t kMaxChance = 100;
        host.goalBumLogic(BumLogicCall{.human = handleArg(args, 0),
                                       .type = unsignedArg(args, 1),
                                       .option = boolArgOr(args, 2, true),
                                       .chance = std::min(unsignedArg(args, 3), kMaxChance),
                                       .value = absent(args, 4) ? 3 : intArg(args, 4),
                                       .callback = nameArg(args, 5),
                                       .option2 = boolArgOr(args, 6, true)});
    });
}

// `GoalMoveToUseFlag(human, flag, gait, delay, duration, radius, reserve)`.
// @orig 0x002db6b0 Goal_MoveToUseFlag (unknown)
NativeFunction makeGoalMoveToUseFlag(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.goalMoveToUseFlag(MoveToUseFlagCall{.human = handleArg(args, 0),
                                                 .flag = handleArg(args, 1),
                                                 .gait = intArg(args, 2),
                                                 .delay = floatArg(args, 3),
                                                 .duration = floatArg(args, 4),
                                                 .radius = floatArg(args, 5),
                                                 .reserve = boolArg(args, 6)});
    });
}

// `SetInterrogateParam(valueA, valueB, valueC, timeA, timeB, timeC, angleA, angleB, timeD, flag, set)`: sets 0-2
// write the first override, 3-5 the second; 6 or more is ignored.
// @orig 0x002854b0 Brain_SetInterrogateOverride (unknown)
NativeFunction makeSetInterrogateParam(const BindingContext& context) {
    return rulesCall(context, [](CharacterRules& rules, std::span<const Value> args) {
        const std::uint32_t set = unsignedArg(args, 10);
        if (set >= kInterrogateSetsPerOverride * rules.interrogate.size()) {
            return;
        }
        constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
        rules.interrogate.at(set / kInterrogateSetsPerOverride) = InterrogateOverride{
            .values = {static_cast<std::uint8_t>(unsignedArg(args, 0)), static_cast<std::uint8_t>(unsignedArg(args, 1)),
                       static_cast<std::uint8_t>(unsignedArg(args, 2))},
            .timesMs = {unsignedArg(args, 3), unsignedArg(args, 4), unsignedArg(args, 5), unsignedArg(args, 8)},
            .anglesRadians = {floatArg(args, 6) * kRadians, floatArg(args, 7) * kRadians},
            .flag = static_cast<std::uint8_t>(unsignedArg(args, 9))};
    });
}

// ---- The gangs.

// `GangAddSpawner(gang, name, arg3, types, model, pos, heading, total, delay, maxConcurrent, kind, target, arg13,
// callback, value, arg16, anim)`: gang -1 does nothing.
// @orig 0x00373dd0 GangAddSpawner (unknown)
NativeFunction makeGangAddSpawner(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        SpawnerCall call;
        call.gang = intArg(args, 0);
        if (call.gang == -1) {
            return;
        }
        call.name = nameArg(args, 1);
        call.arg3 = intArg(args, 2);
        call.model = nameArg(args, 4);
        call.heading = intArg(args, 6);
        call.total = intArg(args, 7);
        call.delayMs = unsignedArg(args, 8);
        call.maxConcurrent = intArg(args, 9);
        call.state = intArg(args, 10);
        call.target = handleArg(args, 11);
        call.arg13 = intArg(args, 12);
        call.callback = nameArg(args, 13);
        call.value = intArg(args, 14);
        call.arg16 = handleArg(args, 15);
        call.anim = nameArg(args, 16);
        const std::array<double, 10> types = tableArg<10>(args, 3);
        std::ranges::transform(types, call.types.begin(), [](double type) { return static_cast<int>(type); });
        const std::array<double, 3> position = tableArg<3>(args, 5);
        std::ranges::transform(position, call.position.begin(), [](double v) { return static_cast<float>(v); });
        host.addSpawner(call);
    });
}

// `GangClearResponders()`.
// @orig 0x0035f668 GangClearResponders (unknown)
NativeFunction makeGangClearResponders(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> /*args*/) { host.clearResponders(); });
}

// `GangClearWanted(gang)`: -1 does nothing.
// @orig 0x0035f630 GangClearWanted (unknown)
NativeFunction makeGangClearWanted(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        if (const int gang = intArg(args, 0); gang != -1) {
            host.clearWanted(gang);
        }
    });
}

// `GangInvincible(gang, on)`: -1 does nothing.
// @orig 0x003732a0 GangInvincible (unknown)
NativeFunction makeGangInvincible(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        if (const int gang = intArg(args, 0); gang != -1) {
            host.setInvincible(gang, boolArg(args, 1));
        }
    });
}

// `GangSetTargetable(gang, on)`: on defaults to true; -1 does nothing.
// @orig 0x0035f728 GangSetTargetable (unknown)
NativeFunction makeGangSetTargetable(const BindingContext& context) {
    return hostCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        if (const int gang = intArg(args, 0); gang != -1) {
            host.setTargetable(gang, boolArgOr(args, 1, true));
        }
    });
}

// ---- The configuration.

// `CfgPlayerMugging(enabled)`: true when omitted.
// @orig 0x0041daf8 Cfg_SetPlayerMugging (unknown)
NativeFunction makeCfgPlayerMugging(const BindingContext& context) {
    return rulesCall(context, [](CharacterRules& rules, std::span<const Value> args) {
        rules.playerMugging = boolArgOr(args, 0, true);
    });
}

// `CfgRageHandlers(enterFn, exitFn, fullFn, durationMs, warnMs)`: the names copied (31 characters), the hold time also
// combat's (CombatTuning::rageHoldMs, the same global).
// @orig 0x00236c58 Cfg_SetRageHandlers (unknown)
NativeFunction makeCfgRageHandlers(const BindingContext& context) {
    return rulesCall(context, [](CharacterRules& rules, std::span<const Value> args) {
        rules.rage = RageHandlers{.onEnter = nameArg(args, 0, kNameLength),
                                  .onExit = nameArg(args, 1, kNameLength),
                                  .onFull = nameArg(args, 2, kNameLength),
                                  .durationMs = unsignedArg(args, 3),
                                  .holdMs = unsignedArg(args, 4)};
        combat::combatTuning().rageHoldMs = static_cast<int>(rules.rage.holdMs);
    });
}

// `CfgSetDefaultFollowSlotSet(set, slots)`: set 0 or 1; the first nine (x, y) pairs of the 20 numbers.
// @orig 0x00294788 Cfg_SetDefaultFollowSlotSet (unknown)
NativeFunction makeCfgSetDefaultFollowSlotSet(const BindingContext& context) {
    return rulesCall(context, [](CharacterRules& rules, std::span<const Value> args) {
        const int set = intArg(args, 0);
        if (set < 0 || set >= static_cast<int>(kDefaultFollowSets)) {
            return;
        }
        constexpr std::size_t kNumbers = 20;
        const std::array<double, kNumbers> numbers = tableArg<kNumbers>(args, 1);
        std::array<std::pair<float, float>, kDefaultFollowSlots> slots{};
        for (std::size_t i = 0; i < slots.size(); ++i) {
            slots.at(i) = {static_cast<float>(numbers.at(2 * i)), static_cast<float>(numbers.at((2 * i) + 1))};
        }
        rules.followSlots.at(static_cast<std::size_t>(set)) = slots;
    });
}

// `CfgSetEnemySpotting(enabled)`: true when omitted.
// @orig 0x0041d738 Cfg_SetEnemySpotting (unknown)
NativeFunction makeCfgSetEnemySpotting(const BindingContext& context) {
    return rulesCall(context, [](CharacterRules& rules, std::span<const Value> args) {
        rules.enemySpotting = boolArgOr(args, 0, true);
    });
}

// `CfgSetWarriorSpotting(enabled)`: the original ignores its argument and always clears the switch.
// @orig 0x0041d748 Cfg_SetWarriorSpotting (unknown)
NativeFunction makeCfgSetWarriorSpotting(const BindingContext& context) {
    return rulesCall(context,
                     [](CharacterRules& rules, std::span<const Value> /*args*/) { rules.warriorSpotting = false; });
}

// `CfgSetGlobalTimeToLive(ms)`.
// @orig 0x0041d4f8 Cfg_SetGlobalTimeToLive (unknown)
NativeFunction makeCfgSetGlobalTimeToLive(const BindingContext& context) {
    return rulesCall(context, [](CharacterRules& rules, std::span<const Value> args) {
        rules.timeToLiveMs = static_cast<std::int32_t>(intArg(args, 0));
    });
}

// A getter's result for a missing human: nil (false), 0, 65535 or NilHandle.
// @orig 0x00235628 Human_IsAlive (unknown)
// @orig 0x00235758 Human_IsPlayer (unknown)
// @orig 0x00235718 Human_IsArrested (unknown)
// @orig 0x00237c38 Human_GetHealthPercent (unknown)
// @orig 0x00235478 Human_GetGangType (unknown)
// @orig 0x00237d48 Human_GetHeldObject (unknown)
void addGetters(LuaVm& vm, const BindingContext& context) {
    using Status = std::optional<HumanStatus>;
    vm.registerFunction("HuIsAlive",
                        statusGetter(context, [](const Status& s) { return binding::boolean(s && s->alive); }));
    vm.registerFunction("HuIsAPlayer",
                        statusGetter(context, [](const Status& s) { return binding::boolean(s && s->player); }));
    vm.registerFunction("HuIsArrested",
                        statusGetter(context, [](const Status& s) { return binding::boolean(s && s->arrested); }));
    vm.registerFunction("HuGetHealthPercent", statusGetter(context, [](const Status& s) {
                            return binding::number(s ? s->healthPercent : 0.0);
                        }));
    vm.registerFunction("HuGetGangType", statusGetter(context, [](const Status& s) {
                            return binding::number(s ? s->gangType : HumanStatus{}.gangType);
                        }));
    vm.registerFunction("HuGetHeldObject", statusGetter(context, [](const Status& s) {
                            return binding::number(s ? s->heldObject : kNilHandle);
                        }));
}

} // namespace

void addHumanBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle) {
    for (const FlagSetter& setter : kFlagSetters) {
        vm.registerFunction(setter.name,
                            hostCall(context, [setter](HumanBindingHost& host, std::span<const Value> args) {
                                const bool on = boolArg(args, 1);
                                host.setFlags(handleArg(args, 0), setter.bits, setter.inverted ? !on : on);
                            }));
    }
    addGetters(vm, context);
    vm.registerFunction("HuSetGodMode", makeSetGodMode(context));
    vm.registerFunction("HuSetDemiGodMode", makeSetDemiGodMode(context));
    vm.registerFunction("HuSetLockedRage", makeSetLockedRage(context));
    vm.registerFunction("HuSetFullRage", makeSetFullRage(context));
    vm.registerFunction("HuSetRageFrac", makeSetRageFrac(context));
    vm.registerFunction("HuSetHealthPercent", makeSetHealthPercent(context));
    vm.registerFunction("HuRevive", makeRevive(context));
    vm.registerFunction("HuSetNormalMode", makeSetNormalMode(context));
    vm.registerFunction("HuSetArrested", makeSetArrested(context));
    vm.registerFunction("HuSetPushable", makeSetPushable(context));
    vm.registerFunction("HuSetMoney", makeSetMoney(context));
    vm.registerFunction("HuSetCarriedItem", makeSetCarriedItem(context));
    vm.registerFunction("HuSetMugCallback", makeSetMugCallback(context));
    vm.registerFunction("HuSetLookTarget", makeSetLookTarget(context));
    vm.registerFunction("HuTeleportNearHuman", makeTeleportNear(context));
    vm.registerFunction("HuLockPad", makeLockPad(context));
    vm.registerFunction("EnableCommand", makeEnableCommand(context));
    vm.registerFunction("EnableCommands", makeEnableCommands(context));
    vm.registerFunction("HuAttachSpinningIcon", makeAttachSpinningIcon(context));
    vm.registerFunction("HuRemoveSpinningIcon", makeRemoveSpinningIcon(context));
    vm.registerFunction("HuDropWeapon", makeDropWeapon(context));
    vm.registerFunction("HuPlaceItemInHand", makePlaceItemInHand(context, std::move(nextHandle)));
    vm.registerFunction("HuUseAnim", makeUseAnim(context));
    vm.registerFunction("SetDynamicAnimation", makeSetDynamicAnimation(context));
    vm.registerFunction("HuChangePlayerGang", makeChangePlayerGang(context));
    vm.registerFunction("WCEnableAllCommands", makeWcEnableAllCommands(context));
    vm.registerFunction("WCIssueCommand", makeWcIssueCommand(context));
    vm.registerFunction("BrClearBackoff", makeClearBackoff(context));
    vm.registerFunction("BrSetThugWantsWeapon", makeSetThugWantsWeapon(context));
    vm.registerFunction("GoalBackoff", makeGoalBackoff(context));
    vm.registerFunction("GoalBumLogic", makeGoalBumLogic(context));
    vm.registerFunction("GoalMoveToUseFlag", makeGoalMoveToUseFlag(context));
    vm.registerFunction("SetInterrogateParam", makeSetInterrogateParam(context));
    vm.registerFunction("GangAddSpawner", makeGangAddSpawner(context));
    vm.registerFunction("GangClearResponders", makeGangClearResponders(context));
    vm.registerFunction("GangClearWanted", makeGangClearWanted(context));
    vm.registerFunction("GangInvincible", makeGangInvincible(context));
    vm.registerFunction("GangSetTargetable", makeGangSetTargetable(context));
    vm.registerFunction("CfgPlayerMugging", makeCfgPlayerMugging(context));
    vm.registerFunction("CfgRageHandlers", makeCfgRageHandlers(context));
    vm.registerFunction("CfgSetDefaultFollowSlotSet", makeCfgSetDefaultFollowSlotSet(context));
    vm.registerFunction("CfgSetEnemySpotting", makeCfgSetEnemySpotting(context));
    vm.registerFunction("CfgSetWarriorSpotting", makeCfgSetWarriorSpotting(context));
    vm.registerFunction("CfgSetGlobalTimeToLive", makeCfgSetGlobalTimeToLive(context));
}

} // namespace coney::script
