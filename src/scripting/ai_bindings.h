// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The AI bindings Coney implements: the goals and actions the level scripts give a human's brain, its switches, its
/// follow slots and the gang tactics. All real; installBindings() registers them.
inline constexpr std::array<std::string_view, 17> kAiBindings{"ActLookAt",
                                                              "BrDead",
                                                              "BrFlush",
                                                              "BrSetFollowSlot",
                                                              "BrSetFollowSlotSet",
                                                              "BrSetNumFollowSlots",
                                                              "BrSetThreatResponse",
                                                              "BrSuspend",
                                                              "GoalAddressPerson",
                                                              "GoalDealer",
                                                              "GoalFight",
                                                              "GoalMoveToFlag",
                                                              "GoalPlayDynAnimation",
                                                              "GoalTrackHuman",
                                                              "TacticClear",
                                                              "TacticCrowd",
                                                              "TacticTrigger"};

/// `GoalMoveToFlag(human, flag, gait, angle, distance, radius, intervalMs, faceFlag, option)` as the binding reads it
/// (docs/references/bindings/ai.md#goalmovetoflag).
struct MoveToFlagCall {
    double human = 0;             ///< The human's handle.
    double flag = 0;              ///< The flag's handle.
    int gait = 0;                 ///< Truncated.
    float angle = 0.0F;           ///< Degrees.
    float distance = 0.0F;        ///< Metres.
    float radius = 0.0F;          ///< Metres.
    std::uint32_t intervalMs = 0; ///< Truncated to an unsigned integer.
    bool faceFlag = false;        ///< nil and 0 are false.
    bool option = false;          ///< nil and 0 are false.
};

/// `ActLookAt(human, target, turnSpeed, timeMs)` as the binding reads it (docs/references/bindings/ai.md#actlookat).
struct LookAtCall {
    double human = 0;          ///< The human's handle.
    double target = 0;         ///< The handle of what it turns to.
    float turn = 0.0F;         ///< The turn value.
    std::int16_t delayMs = -1; ///< The start delay; -1 (the default) is a random 0-500 ms.
};

/// `GoalPlayDynAnimation(human, anim, callback, option)` (docs/references/bindings/ai.md#goalplaydynanimation).
struct DynAnimationCall {
    double human = 0;     ///< The human's handle.
    std::string anim;     ///< The clip's file name; empty does nothing.
    std::string callback; ///< The Lua function called back; empty for none.
    bool option = true;   ///< nil and 0 are false; true when omitted.
};

/// `GoalAddressPerson(human, target, approach, range, speech, callback)`
/// (docs/references/bindings/ai.md#goaladdressperson).
struct AddressPersonCall {
    double human = 0;      ///< Who speaks.
    double target = 0;     ///< Who is addressed.
    float approach = 0.0F; ///< Metres.
    float range = 0.0F;    ///< Metres.
    int speech = -1;       ///< A scene id; negative for none (the default).
    std::string callback;  ///< Empty for none.
};

/// `GoalDealer(human, dealerType, range, runChance, dirtyChance, option)` (docs/references/bindings/ai.md#goaldealer).
struct DealerCall {
    double human = 0;    ///< The dealer.
    int type = 0;        ///< 0, 1 or 2.
    float range = 10.0F; ///< Metres; 10 when omitted.
    int runChance = 50;  ///< Percent; 50 when omitted.
    int dirtyChance = 0; ///< Percent.
    bool option = true;  ///< true when omitted.
};

/// `FlagNetTraverse(human, mode, chance, flagA, flagB)`, or its short form (the defaults).
struct FlagNetTraverseCall {
    double human = 0;
    int mode = 0;       ///< 0 keeps the pace; 1 and 6-9 walk; 2 faster; 3 a third pace.
    int chance = 0;     ///< 0-100.
    bool flagA = false; ///< Meaning not traced.
    bool flagB = false; ///< Meaning not traced.
};

class HumanBindingHost;
class StoryBindingHost;

/// `TacticConfront(gang, targetGang, approachRange, criticalRange, confrontation, slotSet, callback, anim1-5,
/// spotLine)` as the binding reads it (docs/references/bindings/ai.md#tacticconfront); the anims and the spot line are
/// not kept.
struct ConfrontCall {
    int gang = -1;                   ///< Truncated.
    int targetGang = -1;             ///< −1 (the default): the first member's target's gang.
    float approachRange = 10.0F;     ///< Metres; 10 by default.
    float criticalRange = 2.0F;      ///< Metres; 2 by default.
    std::uint32_t confrontation = 0; ///< Truncated to an unsigned integer.
    int slotSet = -1;                ///< −1 by default.
    std::string callback;            ///< Empty for none.
};

/// What the AI and gang bindings ask of the game: the brains of the humans the scripts name by handle, and the gangs
/// by id. A handle that names no brain, or an id no gang, is the host's to ignore, as the original's wrappers do with a
/// handle that is not a human. Every hook but the first two does nothing (or answers 0) by default.
class AiBindingHost {
  public:
    AiBindingHost() = default;
    AiBindingHost(const AiBindingHost&) = delete;
    AiBindingHost& operator=(const AiBindingHost&) = delete;
    AiBindingHost(AiBindingHost&&) = delete;
    AiBindingHost& operator=(AiBindingHost&&) = delete;
    virtual ~AiBindingHost() = default;

    /// Gives the human the move-to-flag goal.
    virtual void goalMoveToFlag(const MoveToFlagCall& call) = 0;
    /// Queues the look-at turn action on the human.
    virtual void actLookAt(const LookAtCall& call) = 0;

    /// `GoalFight(human, target, unused)`: the human fights the target.
    virtual void goalFight(double /*human*/, double /*target*/) {}
    /// `BrFlush(human)`: the goals, then the actions, cleared.
    virtual void brFlush(double /*human*/) {}
    /// `BrDead(human, dead)`.
    virtual void brDead(double /*human*/, bool /*dead*/) {}
    /// `BrSuspend(human, suspended)`.
    virtual void brSuspend(double /*human*/, bool /*suspended*/) {}
    /// `BrSetThreatResponse(human, response)`.
    virtual void brSetThreatResponse(double /*human*/, int /*response*/) {}
    /// `GoalPlayDynAnimation`.
    virtual void goalPlayDynAnimation(const DynAnimationCall& /*call*/) {}
    /// `GoalAddressPerson`.
    virtual void goalAddressPerson(const AddressPersonCall& /*call*/) {}
    /// `GoalTrackHuman(human, target, distance)`.
    virtual void goalTrackHuman(double /*human*/, double /*target*/, float /*distance*/) {}
    /// `GoalDealer`.
    virtual void goalDealer(const DealerCall& /*call*/) {}
    /// `BrSetNumFollowSlots(leader, count, allowed)`; `allowed` -1 takes `count`.
    virtual void brSetNumFollowSlots(double /*leader*/, int /*count*/, int /*allowed*/) {}
    /// `BrSetFollowSlot(leader, slot, {x, y}, set)`.
    virtual void brSetFollowSlot(double /*leader*/, int /*slot*/, float /*x*/, float /*y*/, int /*set*/) {}
    /// `BrSetFollowSlotSet(leader, set)`.
    virtual void brSetFollowSlotSet(double /*leader*/, int /*set*/) {}
    /// `TacticCrowd(gang, callback, cheering)`.
    virtual void tacticCrowd(int /*gang*/, std::string_view /*callback*/, bool /*cheering*/) {}
    /// `TacticTrigger(gang, what, on)`.
    virtual void tacticTrigger(int /*gang*/, int /*what*/, bool /*on*/) {}
    /// `TacticClear(gang)`.
    virtual void tacticClear(int /*gang*/) {}
    /// `TacticAttack(gang, callback)`.
    virtual void tacticAttack(int /*gang*/, std::string_view /*callback*/) {}
    /// `TacticConfront(gang, ...)`.
    virtual void tacticConfront(const ConfrontCall& /*call*/) {}
    /// `BrFlushActions(human)`: the actions cleared, the goals kept.
    virtual void brFlushActions(double /*human*/) {}
    /// `BrFlushGoals(human)`: every goal ended and popped, the actions kept.
    virtual void brFlushGoals(double /*human*/) {}
    /// `HuSetMaxHealth(human, health)`: the maximum and the health set to `health`.
    virtual void setMaxHealth(double /*human*/, int /*health*/) {}
    /// `HuDelete(human)`: the human taken out of the world at once.
    virtual void humanDelete(double /*human*/) {}
    /// `HuGetGang(human)`: its gang's id; nothing for no human or no gang.
    [[nodiscard]] virtual std::optional<int> gangOf(double /*human*/) const { return std::nullopt; }
    /// `HuSwitchPlayer(human)`: when `human` is a player, the pad goes to a team-mate; the new player's handle, or
    /// NilHandle (0) when there is none.
    virtual double switchPlayer(double /*human*/) { return 0.0; }

    /// `GangCreate(kind, name)`: the new gang's id, or -1.
    [[nodiscard]] virtual int gangCreate(int /*kind*/, std::string_view /*name*/) { return -1; }
    /// `GangDelete(gang)`.
    virtual void gangDelete(int /*gang*/) {}
    /// `GangAddMember(gang, index, human)`.
    virtual void gangAddMember(int /*gang*/, double /*human*/) {}
    /// `GangBrDead(gang, dead)`.
    virtual void gangBrDead(int /*gang*/, bool /*dead*/) {}
    /// `GangBrFlush(gang)`.
    virtual void gangBrFlush(int /*gang*/) {}
    /// `GangSetThreatResponse(gang, response)`.
    virtual void gangSetThreatResponse(int /*gang*/, int /*response*/) {}
    /// `GangMakeEnemies(a, b)` and `GangMakeFriends(a, b)`.
    virtual void gangMakeEnemies(int /*a*/, int /*b*/) {}
    virtual void gangMakeFriends(int /*a*/, int /*b*/) {}
    /// `GangSetMsgHandler(gang, message, handler)`; an empty handler clears it.
    virtual void gangSetMsgHandler(int /*gang*/, int /*message*/, std::string_view /*handler*/) {}
    /// `GangSuspend(gang, suspended)`.
    virtual void gangSuspend(int /*gang*/, bool /*suspended*/) {}
    /// `GangGetHeadCount(gang, living)` and `GangGetStandingCount(gang)`.
    [[nodiscard]] virtual int gangHeadCount(int /*gang*/, bool /*living*/) { return 0; }
    [[nodiscard]] virtual int gangStandingCount(int /*gang*/) { return 0; }
    /// `FlagNetTraverse`: the human (not a player's) wanders the level's flag network.
    virtual void flagNetTraverse(const FlagNetTraverseCall& /*call*/) {}

    /// `HuCreate` made `human` (kept in the context's CreatedHumans): from now on its handle names it.
    virtual void humanCreated(const HumanCreation& /*human*/) {}
    /// `TeleportToFlag` put the human with `handle` at `placement`, with no ground snap.
    virtual void humanTeleported(double /*handle*/, const world_objects::Placement& /*placement*/) {}
    /// The level's humans, brains and gangs as the character bindings drive them (scripting/human_bindings.h); null
    /// for none.
    [[nodiscard]] virtual HumanBindingHost* humans() { return nullptr; }
    /// The level's humans, brains, gangs and paths as the story missions' bindings drive them
    /// (scripting/story_bindings.h); null for none.
    [[nodiscard]] virtual StoryBindingHost* story() { return nullptr; }
    /// Where the human with `handle` stands now; nothing when the host has no such human, and the place it was made
    /// or teleported to then stands in (CreatedHumans::placement()).
    [[nodiscard]] virtual std::optional<world_objects::Placement> humanPlacement(double /*handle*/) const {
        return std::nullopt;
    }
};

/// Registers kAiBindings in `vm`, handing each call to `context.ai` (a null one does nothing). They return nothing.
///
/// Research: docs/research/ai.md#scripted, docs/references/bindings/ai.md
void addAiBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
