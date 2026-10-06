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

// The character, AI, gang and Warrior-command bindings the story's second and third missions (`level80`, `level87`)
// add to the first mission's (scripting/human_bindings.h): the humans' removal, health, shadow, head-look and pad
// switches, what they hold and how they are driven; the exit, path, melee, throw and idle goals; the gang tactics; the
// gangs' turf, leaders, ranges and exits; the Warrior commands' dispatcher; the paths, distances and routes between
// objects; and the configuration the missions set. Each acts on the level's humans, brains and gangs through a
// StoryBindingHost (the level's scripted brains, ai::ScriptedStory), the configuration on the game state.
// Research: docs/references/bindings/story.md, docs/references/bindings/character.md, docs/references/bindings/ai.md,
// docs/references/bindings/gang.md, docs/research/ai.md#tactic-kinds

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addStoryBindings().
inline constexpr std::array<std::string_view, 68> kStoryBindings{"AddPath",
                                                                 "BrSetFOV",
                                                                 "BrSetInvestigateResponse",
                                                                 "BrSetReactToViolence",
                                                                 "CfgCivilianAggression",
                                                                 "CfgCrimeResponders",
                                                                 "CfgDisableMusicForScenes",
                                                                 "CfgEnableGrappleCounters",
                                                                 "CfgGangSizeForCombatMusic",
                                                                 "CfgSetOutdoorMode",
                                                                 "CfgTagStartCallback",
                                                                 "CfgVerticalSightModifier",
                                                                 "clearDetailFlag",
                                                                 "CrimeIsHappening",
                                                                 "EnableVolumeBox",
                                                                 "FlagGetOwner",
                                                                 "GangAddTurfBox",
                                                                 "GangCanUseWorldFlags",
                                                                 "GangEnableAttackStrategies",
                                                                 "GangEngageEnemy",
                                                                 "GangExitWorld",
                                                                 "GangGetLeader",
                                                                 "GangIsWanted",
                                                                 "GangRemoveTurfBox",
                                                                 "GangSetHearRange",
                                                                 "GangSetInvestigateResponse",
                                                                 "GangSetLeader",
                                                                 "GangSetRespondPercentage",
                                                                 "GangStartSpawner",
                                                                 "GetDistanceTweenHumans",
                                                                 "GoalBumLogicTrigger",
                                                                 "GoalMelee",
                                                                 "GoalMoveToExitFlag",
                                                                 "GoalPlayDynIdle",
                                                                 "GoalThrowObject",
                                                                 "GoalTravelPath",
                                                                 "HuAreActionsBlocked",
                                                                 "HuBlockLook",
                                                                 "HUDShowWarCommand",
                                                                 "HuExitWorld",
                                                                 "HuForceLook",
                                                                 "HuGetControlName",
                                                                 "HuIsAimingAt",
                                                                 "HuIsGrabbed",
                                                                 "HuKill",
                                                                 "HuLockPadMovement",
                                                                 "HuSetAutoEscape",
                                                                 "HuSetHealth",
                                                                 "HuSetKeepHat",
                                                                 "HuSetLOSRange",
                                                                 "HuSetRevivable",
                                                                 "HuShadow",
                                                                 "HuTagColor",
                                                                 "HuTagPattern",
                                                                 "HuWhatAmIHolding",
                                                                 "IsInsideBox",
                                                                 "IssueWarriorCommand",
                                                                 "PathValid",
                                                                 "SetCharacterModel",
                                                                 "setDetailFlag",
                                                                 "SetFlagPos",
                                                                 "SetSpawnMax",
                                                                 "TestDistance",
                                                                 "WalkingDistance",
                                                                 "WCEnableCommand",
                                                                 "WCIssueCommand",
                                                                 "WCLockCommands",
                                                                 "WCSetCallback"};

/// A position in metres, game axes with z up.
using StoryPoint = std::array<float, 3>;

/// `GoalMoveToExitFlag(human, flag, gait, angle, distance, radius)`; `HuExitWorld` gives the nearest exit flag, gait 4
/// and a 2 m radius.
struct ExitFlagCall {
    double human = 0;
    double flag = 0;
    int gait = 2;
    float angle = 0.0F;
    float distance = 0.0F;
    float radius = 0.3F;
};

/// `GoalTravelPath(human, path, mode, reverse, gait, radius)`.
struct TravelPathCall {
    double human = 0;
    double path = 0; ///< The path's handle (`AddPath`); 0 for none.
    int mode = 0;    ///< At the end: 0 stop, 1 loop, 2 ping-pong.
    bool reverse = false;
    int gait = 2;
    float radius = 0.5F;
};

/// `GoalThrowObject(human, target, range, gait, callback)`.
struct ThrowObjectCall {
    double human = 0;
    double target = 0;
    float range = 16.0F;
    int gait = 2;
    std::string callback;
};

/// `GoalPlayDynIdle(human, flag, startAnim, loopAnim, endAnim, timeMs)`.
struct DynIdleCall {
    double human = 0;
    double flag = 0;
    std::string startAnim;
    std::string loopAnim;
    std::string endAnim;
    int timeMs = -1; ///< -1 for ever.
};

/// The kinds of gang tactic the `Tactic<Name>` bindings set, by the original's type ids
/// (docs/research/ai.md#tactic-kinds).
enum class TacticKind : std::uint8_t {
    Attack = 0x00,
    Defend = 0x02,
    HoldTheLine = 0x04,
    ManWeaponPile = 0x06,
    Pursue = 0x14,
    WalkinTall = 0x15,
    Wander = 0x16,
    TravelPath = 0x17,
    HanginOut = 0x18,
    MoveToFlag = 0x19,
    Vandalize = 0x1c,
    Steal = 0x1d,
    AvoidEnemies = 0x20,
    UseFlag = 0x21,
    Confront = 0x23,
    Idle = 0x24,
    Scout = 0x27,
};

/// One `Tactic<Name>(gang, ...)` call as its binding reads it: the gang, the kind, the callback and the arguments by
/// the meaning the kind gives them (unused ones keep their defaults). Each field names the bindings that set it.
struct TacticCall {
    int gang = -1;
    TacticKind kind = TacticKind::Idle;
    std::string callback;          ///< Every kind: the Lua function called with (gang, code); empty for none.
    std::array<double, 3> flags{}; ///< Handles: the flag (MoveToFlag, WalkinTall, HanginOut, UseFlag), the line's
                                   ///< three flags (HoldTheLine), the human defended (Defend), the path (TravelPath).
    int targetGang = -1;           ///< Pursue, Confront: -1 picks one.
    int gait = 2;                  ///< MoveToFlag, Pursue, Wander, TravelPath, AvoidEnemies (0: the goal's choice).
    int slotSet = -1;              ///< MoveToFlag, Wander, TravelPath, Confront: -1 for set 3.
    float range = 0.0F;            ///< WalkinTall, Pursue, HanginOut, UseFlag, Defend, ManWeaponPile, Scout.
    float range2 = 0.0F;           ///< UseFlag's view, Confront's critical range, AvoidEnemies' max range,
                                   ///< ManWeaponPile's throw range, HoldTheLine's distance, Wander's buffer.
    float range3 = 0.0F;           ///< AvoidEnemies' min / max ally ranges (range3, range4).
    float range4 = 0.0F;
    std::uint32_t delayMs = 0; ///< Vandalize, Steal, TravelPath, Wander, Pursue's search time, HoldTheLine's
                               ///< window (ms).
    std::uint32_t count = 0;   ///< HoldTheLine's hits, Confront's confrontation style, Scout's / ManWeaponPile's
                               ///< values.
    std::uint32_t count2 = 0;
    std::uint32_t zone = 0;         ///< Vandalize, Steal.
    int startPoint = -1;            ///< TravelPath.
    std::array<bool, 6> options{};  ///< By kind (kTacticOption*): banter, respond, full aware, harass, loop, ...
    std::vector<std::string> anims; ///< Confront's five posture anims (empty strings for none).
    std::uint32_t spotLine = 135;   ///< Confront's spot line.
};

/// TacticCall::options' meanings.
inline constexpr std::size_t kTacticBanter = 0;  ///< Pairs of members banter.
inline constexpr std::size_t kTacticRespond = 1; ///< Free members answer violence (HanginOut, Idle).
inline constexpr std::size_t kTacticAware = 2;   ///< HanginOut's full awareness; Idle's clearAnims.
inline constexpr std::size_t kTacticHarass = 3;  ///< HanginOut's harassing; AvoidEnemies' throwables.
inline constexpr std::size_t kTacticLoop = 4;    ///< TravelPath loops; Wander's world flags; Idle's dynIdle.
inline constexpr std::size_t kTacticReverse = 5; ///< TravelPath backwards; Wander's vandal / steal.

/// What the story bindings ask of the level's humans, brains, gangs and paths. A handle that names no human, or an id
/// no gang, is the host's to ignore, as the original's functions do. Every hook does nothing (or answers its
/// default) by default.
class StoryBindingHost {
  public:
    StoryBindingHost() = default;
    StoryBindingHost(const StoryBindingHost&) = delete;
    StoryBindingHost& operator=(const StoryBindingHost&) = delete;
    StoryBindingHost(StoryBindingHost&&) = delete;
    StoryBindingHost& operator=(StoryBindingHost&&) = delete;
    virtual ~StoryBindingHost() = default;

    // ---- Objects and places.

    /// Where the object with `handle` is: a human, a flag or another object; nothing when none has it.
    [[nodiscard]] virtual std::optional<StoryPoint> position(double /*handle*/) const { return std::nullopt; }
    /// The walking distance from `from` to `to` over the level's routes; nothing when either is off the routes or no
    /// route joins them.
    [[nodiscard]] virtual std::optional<float> walkingDistance(double /*from*/, double /*to*/) { return std::nullopt; }
    /// `AddPath`: a path named `name` through the objects `points` (in order; 0s and unknown handles skipped), with
    /// `handle`. Returns false when the 32 path slots are taken.
    virtual bool addPath(double /*handle*/, std::string_view /*name*/, const std::array<double, 8>& /*points*/) {
        return false;
    }

    // ---- The humans.

    /// `HuKill`: the human dies through the damage path.
    virtual void killHuman(double /*human*/) {}
    /// `HuSetHealth`: health in hit points (its maximum when larger).
    virtual void setHealth(double /*human*/, int /*health*/) {}
    /// `HuShadow`.
    virtual void setShadow(double /*human*/, bool /*on*/) {}
    /// `HuLockPadMovement`: the player's left stick reads as centred.
    virtual void lockMovement(double /*human*/, bool /*locked*/) {}
    /// `HuGetControlName`: the human's control handler's name; nothing for no human.
    [[nodiscard]] virtual std::optional<std::string> controlName(double /*human*/) const { return std::nullopt; }
    /// `HuIsAimingAt`.
    [[nodiscard]] virtual bool aimingAt(double /*human*/, double /*target*/) const { return false; }
    /// The name of the object the human holds; empty for none.
    [[nodiscard]] virtual std::string heldObject(double /*human*/) const { return {}; }
    /// `HuIsGrabbed`.
    [[nodiscard]] virtual bool grabbed(double /*human*/) const { return false; }
    /// `HuAreActionsBlocked`.
    [[nodiscard]] virtual bool actionsBlocked(double /*human*/) const { return false; }
    /// `HuSetLOSRange`: the brain's sight range, metres.
    virtual void setSightRange(double /*human*/, float /*range*/) {}
    /// `BrSetFOV`: the view's full width, degrees.
    virtual void setFieldOfView(double /*human*/, float /*degrees*/) {}
    /// `BrSetInvestigateResponse`.
    virtual void setInvestigateResponse(double /*human*/, int /*response*/) {}
    /// `BrSetReactToViolence`.
    virtual void setReactToViolence(double /*human*/, bool /*reacts*/) {}
    /// `HuTagColor`: the colour word.
    virtual void setTagColour(double /*human*/, std::uint32_t /*rgba*/) {}
    /// `HuTag(human, tag, flag)`: the human sprays `tag` from `flag`.
    virtual void tag(double /*human*/, double /*tag*/, double /*flag*/) {}

    // ---- The brains' goals.

    /// `GoalMoveToExitFlag`; `HuExitWorld` with the flag 0 (the host finds it).
    virtual void goalMoveToExitFlag(const ExitFlagCall& /*call*/) {}
    /// `GoalTravelPath`.
    virtual void goalTravelPath(const TravelPathCall& /*call*/) {}
    /// `GoalMelee(human, target)`: target 0 lets it pick.
    virtual void goalMelee(double /*human*/, double /*target*/) {}
    /// `GoalThrowObject`.
    virtual void goalThrowObject(const ThrowObjectCall& /*call*/) {}
    /// `GoalPlayDynIdle`.
    virtual void goalPlayDynIdle(const DynIdleCall& /*call*/) {}
    /// `GoalBumLogicTrigger`.
    virtual void bumTrigger(double /*human*/) {}

    // ---- The gangs.

    /// `GangAddTurfBox` / `GangRemoveTurfBox`.
    virtual void addTurfBox(int /*gang*/, double /*box*/) {}
    virtual void removeTurfBox(int /*gang*/, double /*box*/) {}
    /// `GangEngageEnemy`.
    virtual void engageEnemy(int /*gang*/, double /*target*/) {}
    /// `GangSetInvestigateResponse`.
    virtual void setGangInvestigateResponse(int /*gang*/, int /*response*/) {}
    /// `GangSetRespondPercentage`.
    virtual void setRespondPercentage(int /*gang*/, int /*percent*/) {}
    /// `GangSetHearRange(gang, help, range)`; range -1 restores the default.
    virtual void setHearRange(int /*gang*/, bool /*help*/, float /*range*/) {}
    /// `GangEnableAttackStrategies`.
    virtual void enableAttackStrategies(int /*gang*/, bool /*on*/) {}
    /// `GangSetLeader` / `GangGetLeader` (0 for none).
    virtual void setLeader(int /*gang*/, double /*human*/) {}
    [[nodiscard]] virtual double leader(int /*gang*/) const { return 0; }
    /// `GangExitWorld(gang, exit, callback, deleteGang)`.
    virtual void gangExitWorld(int /*gang*/, double /*exit*/, std::string_view /*callback*/, bool /*deleteGang*/) {}
    /// `GangStartSpawner(gang, name, mode, value)`; value -1 keeps the spawner's.
    virtual void startSpawner(int /*gang*/, std::string_view /*name*/, int /*mode*/, int /*value*/) {}
    /// `GangCanUseWorldFlags(gang, on, percent)`.
    virtual void canUseWorldFlags(int /*gang*/, bool /*on*/, int /*percent*/) {}
    /// A `Tactic<Name>` call: the gang takes the tactic.
    virtual void setTactic(const TacticCall& /*call*/) {}

    // ---- The Warrior commands.

    /// The crew of the war chief `chief` takes Warrior command `command` (the dispatcher's checks passed); with
    /// `forced` the command restarts even when it is the current one. Returns whether it started (false when only the
    /// line was repeated).
    virtual bool startWarriorCommand(double /*chief*/, int /*command*/, bool /*forced*/) { return false; }
    /// Player 1's human's handle (0 for none).
    [[nodiscard]] virtual double playerOne() const { return 0; }
};

/// Registers kStoryBindings in `vm`: the humans', brains', gangs' and paths' bindings act through `context.ai`'s
/// AiBindingHost::story() as it is at each call (none without it: the getters then answer as for a missing human),
/// the configuration on `context.state`, the boxes on `context.boxes`, the flags on `context.flags`. Callbacks run
/// through `scripts`. `nextHandle` gives the handles of the paths `AddPath` makes.
///
/// Research: docs/references/bindings/story.md
void addStoryBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context,
                      std::function<double()> nextHandle);

} // namespace coney::script
