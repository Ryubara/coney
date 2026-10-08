// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "warriors/character_rules.h"

// The character, AI and gang bindings the first mission needs beyond the AI bindings (scripting/ai_bindings.h): the
// humans' flags, health, rage, arrest, pad and commands, what they carry and look at; the back-off, bum and use-flag
// goals; the gangs' invincibility, targeting, spawners and wanted state; and the configuration of the characters'
// rules. Each acts on the level's humans, brains and gangs through a HumanBindingHost (the level's scripted brains),
// and the configuration on the game state's CharacterRules, which a fresh state and the boot's scripts share.
// Research: docs/references/bindings/character.md, docs/references/bindings/ai.md, docs/references/bindings/gang.md,
// docs/references/bindings/config.md

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addHumanBindings().
inline constexpr std::array<std::string_view, 65> kHumanBindings{"BrClearBackoff",
                                                                 "BrSetThugWantsWeapon",
                                                                 "CfgPlayerMugging",
                                                                 "CfgRageHandlers",
                                                                 "CfgSetDefaultFollowSlotSet",
                                                                 "CfgSetEnemySpotting",
                                                                 "CfgSetGlobalTimeToLive",
                                                                 "CfgSetWarriorSpotting",
                                                                 "EnableCommand",
                                                                 "EnableCommands",
                                                                 "GangAddSpawner",
                                                                 "GangClearResponders",
                                                                 "GangClearWanted",
                                                                 "GangInvincible",
                                                                 "GangSetTargetable",
                                                                 "GoalBackoff",
                                                                 "GoalBumLogic",
                                                                 "GoalMoveToUseFlag",
                                                                 "GoalTag",
                                                                 "HuAttachSpinningIcon",
                                                                 "HuChangePlayerGang",
                                                                 "HuDropWeapon",
                                                                 "HuGetGangType",
                                                                 "HuGetHealthPercent",
                                                                 "HuGetHeldObject",
                                                                 "HuHasHat",
                                                                 "HuIsAPlayer",
                                                                 "HuIsAlive",
                                                                 "HuIsArrested",
                                                                 "HuLockPad",
                                                                 "HuPlaceHatOnHead",
                                                                 "HuPlaceItemInHand",
                                                                 "HuRemoveSpinningIcon",
                                                                 "HuRevive",
                                                                 "HuSetArrested",
                                                                 "HuSetCarriedItem",
                                                                 "HuSetDemiGodMode",
                                                                 "HuSetFastClimber",
                                                                 "HuSetFullRage",
                                                                 "HuSetGodMode",
                                                                 "HuSetHealthPercent",
                                                                 "HuSetIncreasedReact",
                                                                 "HuSetKeepWeapon",
                                                                 "HuSetLockedRage",
                                                                 "HuSetLookTarget",
                                                                 "HuSetMoney",
                                                                 "HuSetMugCallback",
                                                                 "HuSetNoTarget",
                                                                 "HuSetNoThrowWeapon",
                                                                 "HuSetNormalMode",
                                                                 "HuSetPreventRage",
                                                                 "HuSetPushable",
                                                                 "HuSetRageFrac",
                                                                 "HuSetReducedReact",
                                                                 "HuSetTireless",
                                                                 "HuSetUngrabbable",
                                                                 "HuSetUngroundable",
                                                                 "HuSetUnstunnable",
                                                                 "HuTeleportNearHuman",
                                                                 "HuUseAnim",
                                                                 "HuUseAnyAnim",
                                                                 "LoadBumAnims",
                                                                 "SetDynamicAnimation",
                                                                 "SetInterrogateParam",
                                                                 "WCEnableAllCommands"};

/// What a human handle names, for the getters: nothing when it names no human.
struct HumanStatus {
    bool alive = false;         ///< `HuIsAlive`.
    bool player = false;        ///< `HuIsAPlayer`: it has a player index.
    bool arrested = false;      ///< `HuIsArrested`.
    float healthPercent = 0.0F; ///< `HuGetHealthPercent`.
    int gangType = 0xffff;      ///< `HuGetGangType`: its gang's kind; 65535 for none.
    double heldObject = 0;      ///< `HuGetHeldObject`: NilHandle (0) for none.
    double hat = 0;             ///< `HuHasHat`: the hat worn, NilHandle (0) for none.
    bool soundCommands = true;  ///< May say speech commands (`HuEnableSoundCommands`).
};

/// `HuSetLookTarget(human, target, ms, weight, flagA, flagB)` as the binding reads it.
struct LookTargetCall {
    double human = 0;
    double target = 0;
    std::int64_t timeMs = 2000; ///< -1 for no limit.
    float weight = 1.0F;
    std::uint32_t options = 0; ///< `0x20` (flagA) and `0x40` (flagB).
};

/// `GoalBackoff(human, from, distance, timeMs, option)`.
struct BackoffCall {
    double human = 0;
    double from = 0;
    float distance = 0.0F;
    std::int32_t timeMs = -1; ///< -1 for no limit.
    bool option = true;
};

/// `GoalBumLogic(human, bumType, option, chance, value, callback, option2)`.
struct BumLogicCall {
    double human = 0;
    std::uint32_t type = 0;
    bool option = true;
    std::uint32_t chance = 0; ///< Clamped to 100.
    int value = 3;
    std::string callback;
    bool option2 = true;
};

/// `GoalMoveToUseFlag(human, flag, gait, delay, duration, radius, reserve)`.
struct MoveToUseFlagCall {
    double human = 0;
    double flag = 0;
    int gait = 2;
    float delay = 0.0F;
    float duration = 0.0F;
    float radius = 0.0F;
    bool reserve = false;
};

/// `GoalTag(human, flag, tag, extra)`.
struct TagCall {
    double human = 0;
    double flag = 0;  ///< The flag to spray from; the goal claims it.
    double tag = 0;   ///< The tag object sprayed.
    double extra = 0; ///< Another tag object made blank (message `0x19` with 4) as the spray starts; 0 for none.
};

/// `GangAddSpawner(gang, name, arg3, types, model, pos, heading, total, delay, maxConcurrent, kind, target, arg13,
/// callback, value, arg16, anim)`: the fields the research names.
struct SpawnerCall {
    int gang = -1;
    std::string name;
    int arg3 = 0;
    std::array<int, 10> types{};
    std::string model;
    std::array<float, 3> position{};
    int heading = 0;
    int total = -1;
    std::uint32_t delayMs = 0;
    int maxConcurrent = 0;
    int state = 0;
    double target = 0;
    int arg13 = 0;
    std::string callback;
    int value = 0;
    double arg16 = 0;
    std::string anim;
};

/// What the bindings ask of the level's humans, brains and gangs. A handle that names no human, or an id no gang, is
/// the host's to ignore, as the original's functions do. Every hook does nothing (or answers its default) by default.
class HumanBindingHost {
  public:
    HumanBindingHost() = default;
    HumanBindingHost(const HumanBindingHost&) = delete;
    HumanBindingHost& operator=(const HumanBindingHost&) = delete;
    HumanBindingHost(HumanBindingHost&&) = delete;
    HumanBindingHost& operator=(HumanBindingHost&&) = delete;
    virtual ~HumanBindingHost() = default;

    // ---- The humans (docs/references/bindings/character.md).

    /// What the human with `handle` is now; nothing when no human has it.
    [[nodiscard]] virtual std::optional<HumanStatus> status(double /*handle*/) const { return std::nullopt; }
    /// Sets (`on`) or clears `bits` of the human's flag word (`HuSetGodMode`, `HuSetTireless`, ...).
    virtual void setFlags(double /*human*/, std::uint64_t /*bits*/, bool /*on*/) {}
    /// `HuSetLockedRage`.
    virtual void setLockedRage(double /*human*/, bool /*on*/) {}
    /// `HuSetFullRage`: the meter held for `holdMs`.
    virtual void fillRage(double /*human*/, int /*holdMs*/) {}
    /// `HuSetRageFrac`.
    virtual void setRageFraction(double /*human*/, float /*fraction*/) {}
    /// `HuSetHealthPercent`.
    virtual void setHealthPercent(double /*human*/, float /*percent*/) {}
    /// `HuRevive`.
    virtual void revive(double /*human*/) {}
    /// `HuSetNormalMode`.
    virtual void setNormalMode(double /*human*/, bool /*full*/) {}
    /// `HuSetRageMode(human, on)`.
    virtual void setRageMode(double /*human*/, bool /*on*/) {}
    /// `HuMarkReachable(human, reachable)`.
    virtual void markReachable(double /*human*/, bool /*reachable*/) {}
    /// `HuIsTagging(human)`: whether the human is spraying a tag now.
    [[nodiscard]] virtual bool tagging(double /*human*/) const { return false; }
    /// `HuSetArrested`.
    virtual void setArrested(double /*human*/, bool /*arrested*/) {}
    /// `HuSetPushable`.
    virtual void setPushable(double /*human*/, bool /*pushable*/) {}
    /// `HuSetMoney`: already clamped to 0-999.
    virtual void setMoney(double /*human*/, int /*dollars*/) {}
    /// `HuSetCarriedItem`.
    virtual void setCarriedItem(double /*human*/, std::string_view /*object*/) {}
    /// `HuSetMugCallback`: empty clears it.
    virtual void setMugCallback(double /*human*/, std::string_view /*callback*/) {}
    /// `HuSetConscious`: false knocks the human out (once), true brings it round.
    virtual void setConscious(double /*human*/, bool /*conscious*/) {}
    /// `HuEnableSoundCommands`.
    virtual void setSoundCommands(double /*human*/, bool /*on*/) {}
    /// `HuPutItemInPocket` (item 0 and count 0 for `HuRemoveItemInPocket`).
    virtual void setPocket(double /*human*/, int /*item*/, int /*count*/) {}
    /// `HuSetLookTarget`.
    virtual void setLookTarget(const LookTargetCall& /*call*/) {}
    /// `HuTeleportNearHuman`.
    virtual void teleportNear(double /*human*/, double /*near*/) {}
    /// `HuLockPad`.
    virtual void lockPad(double /*human*/, bool /*locked*/) {}
    /// `EnableCommand` (`command` 1-57) and `EnableCommands` (every command: `command` 0).
    virtual void enableCommand(double /*human*/, int /*command*/, bool /*enabled*/) {}
    /// `HuAttachSpinningIcon` and `HuRemoveSpinningIcon` (an empty `object`).
    virtual void setIcon(double /*human*/, std::string_view /*object*/, int /*param*/) {}
    /// `HuDropWeapon`.
    virtual void dropWeapon(double /*human*/) {}
    /// `ObjDestroy` of `object`: a human holding it lets go of it first.
    virtual void releaseObject(double /*object*/) {}
    /// `HuPlaceItemInHand`: makes the object `object` (its handle from `nextHandle`) in the human's hand; returns the
    /// handle, or NilHandle (0) when the human is missing or holds something.
    virtual double placeItemInHand(double /*human*/, std::string_view /*object*/,
                                   const std::function<double()>& /*nextHandle*/) {
        return 0;
    }
    /// `HuPlaceHatOnHead`: puts a new hat of type `hat` (its handle from `nextHandle`) on the human's head.
    virtual void placeHatOnHead(double /*human*/, std::string_view /*hat*/,
                                const std::function<double()>& /*nextHandle*/) {}
    /// `HuUseAnim`: replaces animation slot `slot` (0-3) with `anim` (empty removes it), `loaded` when the level asked
    /// for the file (`SetDynamicAnimation`). Returns whether the clip is in place.
    virtual bool useAnim(double /*human*/, int /*slot*/, std::string_view /*anim*/, bool /*loaded*/) { return false; }
    /// `HuUseAnyAnim`: puts `anim` in the human's slot for anim `animId` (empty frees it), `loaded` when the level
    /// asked for the file; whether it was set (or freed).
    virtual bool useAnyAnim(double /*human*/, std::uint32_t /*animId*/, std::string_view /*anim*/, bool /*loaded*/) {
        return false;
    }
    /// `HuChangePlayerGang`: the players take over members of gang `gang`; with `stamp` the game time is noted.
    virtual void changePlayerGang(int /*gang*/, bool /*stamp*/) {}
    /// Who has reserved the flag with `handle` to use it (`GoalMoveToUseFlag`); 0 for no one.
    [[nodiscard]] virtual double flagUser(double /*flag*/) const { return 0; }
    /// Which player (0 or 1) the human with `handle` is; nothing for a human no player controls.
    [[nodiscard]] virtual std::optional<int> playerIndex(double /*handle*/) const { return std::nullopt; }

    // ---- The brains (docs/references/bindings/ai.md).

    /// `BrClearBackoff`.
    virtual void clearBackoff(double /*human*/) {}
    /// `BrSetThugWantsWeapon`.
    virtual void setWantsWeapon(double /*human*/, bool /*wants*/) {}
    /// `BrSetDamageResponse`.
    virtual void setDamageResponse(double /*human*/, int /*response*/) {}
    /// `GoalBackoff`.
    virtual void goalBackoff(const BackoffCall& /*call*/) {}
    /// `GoalBumLogic`.
    virtual void goalBumLogic(const BumLogicCall& /*call*/) {}
    /// `GoalMoveToUseFlag`.
    virtual void goalMoveToUseFlag(const MoveToUseFlagCall& /*call*/) {}
    /// `GoalTag`.
    virtual void goalTag(const TagCall& /*call*/) {}

    // ---- The gangs (docs/references/bindings/gang.md).

    /// `GangAddSpawner`.
    virtual void addSpawner(const SpawnerCall& /*call*/) {}
    /// `GangClearResponders`.
    virtual void clearResponders() {}
    /// `GangClearWanted`.
    virtual void clearWanted(int /*gang*/) {}
    /// `GangInvincible`.
    virtual void setInvincible(int /*gang*/, bool /*on*/) {}
    /// `GangSetTargetable`.
    virtual void setTargetable(int /*gang*/, bool /*on*/) {}
    /// `GangSetDamageResponse`: every current member's.
    virtual void setGangDamageResponse(int /*gang*/, int /*response*/) {}
    /// `GangAttachSpinningIcon` and `GangRemoveSpinningIcon` (an empty `object`): every current member's icon, as
    /// setIcon() gives one human's.
    virtual void setGangIcon(int /*gang*/, std::string_view /*object*/, int /*param*/) {}

    /// The game state's characters' rules changed (a configuration binding): the host takes what it uses of them.
    virtual void applyRules(const CharacterRules& /*rules*/) {}
    /// `CfgSetDefaultFollowSlotSet`: set `set`'s default follow slots, (x, y) m, written into every formation in use
    /// as well as kept for those made later. Only this binding touches the formations in use; the other configuration
    /// bindings' applyRules() must not (docs/research/ai.md#level99-snaps).
    virtual void setDefaultFollowSlots(int /*set*/, std::span<const std::pair<float, float>> /*slots*/) {}
};

/// Registers kHumanBindings in `vm`: the humans', brains' and gangs' bindings act through `context.ai`'s
/// AiBindingHost::humans() (none without it; the getters then answer as for a missing human), the configuration on
/// `context.state`'s rules. `nextHandle` gives the handles of the objects `HuPlaceItemInHand` makes.
///
/// Research: docs/references/bindings/character.md, docs/references/bindings/ai.md, docs/references/bindings/gang.md,
/// docs/references/bindings/config.md
void addHumanBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle);

} // namespace coney::script
