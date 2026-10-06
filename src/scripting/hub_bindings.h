// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"

// The character, AI and gang bindings the hub (`level95`, the Warriors' clubhouse and Coney between missions) adds to
// the story missions' (scripting/story_bindings.h): the humans' gear, size, names, money, cuffs, mugging and arrest
// switches, their fight stance, their dynamic clip and their workouts on the clubhouse's equipment; the brains' type,
// world flags and enemies; the strolling, boxing, holding, vending, generic-clip and shop-keeping goals; and the gangs'
// flight, handlers, readiness, spawners, bums and neutrality. Each acts on the level's humans, brains and gangs through
// a HubBindingHost (the level's scripted brains, ai::ScriptedHub); the workout tuning on the game state.
// Research: docs/references/bindings/story.md#level95, docs/references/bindings/character.md,
// docs/references/bindings/ai.md, docs/references/bindings/gang.md

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addHubBindings().
inline constexpr std::array<std::string_view, 35> kHubBindings{"BrCanUseWorldFlags",
                                                               "BrHasEnemies",
                                                               "CfgWorkoutParams",
                                                               "GangCanFlee",
                                                               "GangClearBums",
                                                               "GangClearHandlers",
                                                               "GangGoodToGo",
                                                               "GangIsASpawner",
                                                               "GangMakeNeutralOfType",
                                                               "GoalAreaWalker",
                                                               "GoalBoxer",
                                                               "GoalGrabTarget",
                                                               "GoalPeddler",
                                                               "GoalPlayGenAnim",
                                                               "GoalShopkeeper",
                                                               "HuAttachGear",
                                                               "HuBlockTackle",
                                                               "HuGetMoney",
                                                               "HuGetVoiceIndex",
                                                               "HuGiveCuffs",
                                                               "HuIsDead",
                                                               "HuPlayDynAnim",
                                                               "HuSetCombatMode",
                                                               "HuSetLookAtTarget",
                                                               "HuSetMug",
                                                               "HuSetName",
                                                               "HuSetPedReaction",
                                                               "HuSetScale",
                                                               "HuSetUnarrestable",
                                                               "HuSetWorkoutBlend",
                                                               "HuSetWorkoutCallbacks",
                                                               "HuStopWorkout",
                                                               "HuWorkout",
                                                               "ObjIsAlive",
                                                               "GetObjectName"};

/// A position in metres, game axes with z up.
using HubPoint = std::array<float, 3>;

/// What a human handle names, for the hub's getters: nothing when it names no human.
struct HubHumanStatus {
    int money = 0;        ///< `HuGetMoney`: cash carried (`+0x370`).
    HubPoint position{};  ///< Where the human stands.
    bool dead = false;    ///< `HuIsDead`: out of health.
    bool player = false;  ///< Driven by a pad (player 1).
    int playerIndex = -1; ///< Its player index, -1 for an AI human.
};

/// `HuWorkout(human, equipment, startAnim, endAnim, loop1, loop2, loop3)`.
struct WorkoutCall {
    double human = 0;
    double equipment = 0;
    std::array<std::string, 5> clips; ///< start (691), end (692), loops 1-3 (693-695).
};

/// `GoalAreaWalker(human, flag, radius, mode, durationSeconds, pauseSeconds)`.
struct AreaWalkerCall {
    double human = 0;
    double flag = 0; ///< 0 (NilHandle): where the human stands.
    int radius = 0;
    int mode = 0;
    std::uint32_t durationSeconds = 0; ///< 0 for ever.
    int pauseSeconds = 0;
};

/// `GoalPeddler(human, range, reacts, greetAnim, idleAnim)`.
struct PeddlerCall {
    double human = 0;
    float range = 0.0F;
    bool reacts = false;
    std::string greetAnim;
    std::string idleAnim;
};

/// `GoalShopkeeper(human, store, kind, broom, range, onDisturbed, onPhone, pleads)`.
struct ShopkeeperCall {
    double human = 0;
    double store = 0; ///< The store's volume box.
    int kind = 0;     ///< 1 phones, 2 fights, 3 cowers, 4 cowers and never pleads; 0 picks 2 or 3.
    bool broom = false;
    float range = 9.0F;
    std::string onDisturbed;
    std::string onPhone;
    bool pleads = true;
};

/// What the hub's bindings ask of the level's humans, brains and gangs. A handle that names no human, or an id no gang,
/// is the host's to ignore, as the original's functions do. Every hook does nothing (or answers its default) by
/// default.
class HubBindingHost {
  public:
    HubBindingHost() = default;
    HubBindingHost(const HubBindingHost&) = delete;
    HubBindingHost& operator=(const HubBindingHost&) = delete;
    HubBindingHost(HubBindingHost&&) = delete;
    HubBindingHost& operator=(HubBindingHost&&) = delete;
    virtual ~HubBindingHost() = default;

    // ---- The humans.

    /// The human `handle` names; nothing for none.
    [[nodiscard]] virtual std::optional<HubHumanStatus> status(double /*handle*/) const { return std::nullopt; }
    /// Whether `handle` names a live object the host knows (a human, a flag).
    [[nodiscard]] virtual bool alive(double /*handle*/) const { return false; }
    /// `HuAttachGear`: a player's knuckles and boots, each pair only when `knuckles` / `boots` is unlocked.
    virtual void attachGear(double /*human*/, bool /*attach*/, bool /*knuckles*/, bool /*boots*/) {}
    /// `HuSetScale`.
    virtual void setScale(double /*human*/, float /*scale*/) {}
    /// `HuSetName`: the name the human is found by.
    virtual void setName(double /*human*/, std::string_view /*name*/) {}
    /// `HuGiveCuffs` on an AI human: its own cuff count, clamped to 0-9.
    virtual void addCuffs(double /*human*/, int /*count*/) {}
    /// `HuSetMug`.
    virtual void setMuggable(double /*human*/, bool /*on*/) {}
    /// `HuSetPedReaction`: the reaction style, 16 bits sign-extended.
    virtual void setPedReaction(double /*human*/, int /*reaction*/) {}
    /// `HuSetUnarrestable`.
    virtual void setUnarrestable(double /*human*/, bool /*on*/) {}
    /// `HuSetCombatMode`.
    virtual void setCombatMode(double /*human*/, bool /*on*/) {}
    /// `HuPlayDynAnim`: the dynamic clip `anim` plays at once.
    virtual void playDynAnim(double /*human*/, std::string_view /*anim*/) {}
    /// `HuWorkout`.
    virtual void workout(const WorkoutCall& /*call*/) {}
    /// `HuStopWorkout`.
    virtual void stopWorkout(double /*human*/) {}
    /// `HuSetWorkoutBlend`.
    virtual void setWorkoutBlend(double /*human*/, float /*blend*/) {}

    // ---- The brains.

    /// `BrCanUseWorldFlags(human, allowed, chance)`.
    virtual void canUseWorldFlags(double /*human*/, bool /*allowed*/, int /*chance*/) {}
    /// `BrHasEnemies`.
    [[nodiscard]] virtual bool hasEnemies(double /*human*/) const { return false; }

    // ---- The goals.

    virtual void goalAreaWalker(const AreaWalkerCall& /*call*/) {}
    virtual void goalBoxer(double /*human*/, double /*target*/) {}
    virtual void goalGrabTarget(double /*human*/, double /*target*/) {}
    virtual void goalPeddler(const PeddlerCall& /*call*/) {}
    /// `GoalPlayGenAnim(human, anim, callback)`.
    virtual void goalPlayGenAnim(double /*human*/, int /*anim*/, std::string_view /*callback*/) {}
    virtual void goalShopkeeper(const ShopkeeperCall& /*call*/) {}

    // ---- The gangs.

    /// `GangCanFlee`.
    virtual void canFlee(int /*gang*/, bool /*on*/) {}
    /// `GangClearBums`: player 1's gang's members of category kBumCategory are destroyed.
    virtual void clearBums() {}
    /// The handles of the gang's members (for `GangClearHandlers`).
    [[nodiscard]] virtual std::vector<double> memberHandles(int /*gang*/) const { return {}; }
    /// `GangClearHandlers`: the gang's own message handlers.
    virtual void clearGangHandlers(int /*gang*/) {}
    /// `GangGoodToGo`.
    [[nodiscard]] virtual bool goodToGo(int /*gang*/, bool /*ignoreBusy*/) const { return false; }
    /// `GangIsASpawner`.
    [[nodiscard]] virtual bool isASpawner(int /*gang*/, std::string_view /*name*/) const { return false; }
    /// `GangMakeNeutralOfType`.
    virtual void makeNeutralOfType(int /*gang*/, int /*type*/) {}
};

/// The `CfgChar` category (`+0x11b`) of the bums, which `GangClearBums` sends away and `GangGoodToGo` skips.
inline constexpr int kBumCategory = 6;
/// The category of the members that flee once their gang is beaten down (`GangCanFlee`).
inline constexpr int kFleeCategory = 11;

/// Registers kHubBindings in `vm`: the humans', brains' and gangs' bindings act through `context.ai`'s
/// AiBindingHost::hub() as it is at each call (none without it: the getters then answer as for a missing human), the
/// workout tuning on `context.state`, the handlers on `context.messages`, the objects on `context.spawnRecords` and
/// `context.flags`. The inventory callback of `HuGiveCuffs` runs through `scripts`.
///
/// Research: docs/references/bindings/story.md#level95
void addHubBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context);

} // namespace coney::script
