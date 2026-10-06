// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ai/hub_goals.h"
#include "scripting/hub_bindings.h"
#include "warriors/hub_state.h"

// The level scripts' hold on the humans, brains and gangs for the hub's bindings (scripting/hub_bindings.h): the
// humans the scripts name by handle (ai::ScriptedBrains) get their gear, size, money, cuffs and switches and their
// workouts; the brains their type, world flags and the hub's goals; the gangs their flight, readiness and neutrality.
// Like the other bindings, a call on a human not made yet waits until the level makes it (ScriptedBrains::hold()).
// After each characters' step update() runs the workouts, refreshes the players' enemy counts and lets beaten gangs
// flee.
// Research: docs/references/bindings/story.md#level95, docs/references/bindings/character.md#huworkout

namespace coney::ai {

class Brain;
class Gang;
class ScriptedBrains;

/// What the hub's host reads from the level beyond the brains: the configuration's categories and flee percentages,
/// the workout's tuning, the volume boxes, the flags and the crimes. Each may be empty (null): the host does without.
struct HubLookups {
    /// A character type's `CfgChar` category (`+0x11b`); nothing when the type has none.
    std::function<std::optional<int>(int type)> category;
    /// A gang kind's flee percentage (`CfgGang`'s last byte, `+0x72`); 0 for none.
    std::function<int(int kind)> fleePercent;
    /// The workout's tuning and callbacks.
    const WorkoutSettings* workout = nullptr;
    /// Whether a point is inside a volume box, and the flags inside one.
    std::function<bool(double box, anim::Vec3 point)> inBox;
    std::function<std::vector<anim::Vec3>(double box)> flagsInBox;
    /// The crimes reported so far, the last one's place, and a break-in report.
    std::function<std::uint64_t()> crimeCount;
    std::function<std::optional<anim::Vec3>()> lastCrime;
    std::function<void(anim::Vec3 at)> reportBreakIn;
    /// Where the `CrimeScene` flag is.
    std::function<std::optional<anim::Vec3>()> crimeScene;
};

/// One human's workout (`HuWorkout`): the state code `0x1b` until it begins, then the working-out flag.
struct WorkoutRun {
    double equipment = 0;
    std::array<std::string, 5> clips;
    bool begun = false;          ///< Working out (state flag `0x20000000000`); before, it waits to begin.
    float blend = 1.0F;          ///< The effort (`+0x670`): an AI's from `HuSetWorkoutBlend`, a player's 0-1 × 3.
    float effort = 1.0F / 3.0F;  ///< A player's own effort, 0-1, which the pad pumps.
    std::uint64_t nextRepMs = 0; ///< When the next repetition's callback is due.
};

/// The hub bindings' host on the brains a level's scripts drive.
class ScriptedHub final : public script::HubBindingHost {
  public:
    /// How near the equipment a workout begins, m (**Coney stand-in** for the start clip's reach, not on the page).
    static constexpr float kWorkoutReach = 1.5F;
    /// A repetition's length at an effort of 1, ms (**Coney stand-in**: the loop clip's timing is not traced).
    static constexpr std::uint64_t kRepMs = 1500;
    /// How often a player's brain counts the AI humans after him (`BrHasEnemies`), in brain updates.
    static constexpr std::uint64_t kEnemyCountPeriod = 300;

    /// The hold on `scripted`'s brains (which must outlive it).
    explicit ScriptedHub(ScriptedBrains& scripted);

    /// What the host reads from the level.
    void setLookups(HubLookups lookups) { m_lookups = std::move(lookups); }

    /// After each characters' step at game time `nowMs`: the workouts begin, pump, repeat and end; each player's count
    /// of AI humans after him is refreshed every kEnemyCountPeriod brain updates; a fleeing gang beaten below its
    /// percentage sends its fleeing members off.
    /// @orig 0x00255540 Human_UpdateWorkout (unknown)
    /// @orig 0x003035d8 PlayerBrain_Update (unknown)
    /// @orig 0x00307d40 GangTactic_CheckFlee (unknown)
    void update(std::uint64_t nowMs);

    // ---- script::HubBindingHost: each does nothing for a handle with no human or an id with no gang ----

    [[nodiscard]] std::optional<script::HubHumanStatus> status(double handle) const override;
    /// A bound human or a flag.
    [[nodiscard]] bool alive(double handle) const override;
    /// A player's only.
    /// @orig 0x00236188 Human_SetUnlockedGear (unknown)
    void attachGear(double human, bool attach, bool knuckles, bool boots) override;
    /// @orig 0x0023b0e0 Human_SetScale (unknown)
    void setScale(double human, float scale) override;
    /// @orig 0x00222980 Human_AddCuffs (unknown)
    void addCuffs(double human, int count) override;
    /// @orig 0x00239ee0 Human_SetMug (unknown)
    void setMuggable(double human, bool on) override;
    /// @orig 0x00238640 Human_SetPedReaction (unknown)
    void setPedReaction(double human, int reaction) override;
    /// Flag::kUnarrestable and the uncuff offer (**Coney stand-in**: no prompt yet).
    /// @orig 0x00235e70 Human_SetUnarrestable (unknown)
    void setUnarrestable(double human, bool on) override;
    /// @orig 0x0023a1b0 Human_SetCombatMode (unknown)
    void setCombatMode(double human, bool on) override;
    /// The dynamic clip asked for and anim 668 played through the scripts' services (**Coney stand-in**: Coney's
    /// humans play no clip by id from outside their dispatcher yet). Refused for a human out of health.
    /// @orig 0x00238948 Human_PlayDynAnim (unknown)
    void playDynAnim(double human, std::string_view anim) override;
    /// @orig 0x00238bd8 Human_StartWorkout (unknown)
    void workout(const script::WorkoutCall& call) override;
    /// @orig 0x00238d30 Human_StopWorkout (unknown)
    void stopWorkout(double human) override;
    /// @orig 0x00238cd8 Human_SetWorkoutBlend (unknown)
    void setWorkoutBlend(double human, float blend) override;

    /// @orig 0x00292c10 Brain_SetWorldFlagUse (unknown)
    void canUseWorldFlags(double human, bool allowed, int chance) override;
    /// @orig 0x00292c68 Brain_HasEnemies (unknown)
    [[nodiscard]] bool hasEnemies(double human) const override;

    /// @orig 0x002a4cb8 Goal_AreaWalker (unknown)
    void goalAreaWalker(const script::AreaWalkerCall& call) override;
    /// @orig 0x002d97c0 Goal_Boxer (unknown)
    void goalBoxer(double human, double target) override;
    /// @orig 0x002bb3d0 Goal_GrabTarget (unknown)
    void goalGrabTarget(double human, double target) override;
    /// @orig 0x002ad2c0 Goal_Peddler (unknown)
    void goalPeddler(const script::PeddlerCall& call) override;
    /// @orig 0x002d4628 Goal_PlayGenericAnimation (unknown)
    void goalPlayGenAnim(double human, int anim, std::string_view callback) override;
    /// @orig 0x002e5a00 Goal_Shopkeeper (unknown)
    void goalShopkeeper(const script::ShopkeeperCall& call) override;

    /// The gang's flight on or off; on, the members it has now are its starting count (**Coney's**: the original notes
    /// the count when a tactic starts, which Coney's tactics do not report).
    /// @orig 0x0016bcf0 Gang_SetCanFlee (unknown)
    /// @orig 0x00307fe0 TacticAttack_Start (unknown)
    void canFlee(int gang, bool on) override;
    /// @orig 0x0016b8d0 Gang_ClearBums (unknown)
    void clearBums() override;
    [[nodiscard]] std::vector<double> memberHandles(int gang) const override;
    /// @orig 0x001647e0 Gang_ClearMsgHandlers (unknown)
    void clearGangHandlers(int gang) override;
    /// **Coney's reading** of the five busy states: held or holding someone, grounded or out of health; the fight test
    /// is an attacker's slot on the member or a target of its own.
    /// @orig 0x0016a4e8 Gang_IsGoodToGoById (unknown)
    [[nodiscard]] bool goodToGo(int gang, bool ignoreBusy) const override;
    /// A spawner of the gang with that name that is still in use (it has not made its total).
    /// @orig 0x0016b618 Gang_HasSpawnerById (unknown)
    [[nodiscard]] bool isASpawner(int gang, std::string_view name) const override;
    void makeNeutralOfType(int gang, int type) override;

    /// The lines the hub's goals have had their humans say (SaidLine).
    [[nodiscard]] const std::vector<SaidLine>& said() const { return m_said; }
    /// The workout of the human `handle` names; null for none.
    [[nodiscard]] const WorkoutRun* workoutOf(double handle) const;

  private:
    // Runs `body` on the brain named by `handle` now, or when the level makes it while calls are held.
    void onBrain(double handle, const std::function<void(Brain&)>& body);
    // Calls the Lua function `name` with the human's handle, when it is set.
    void callBack(const std::string& name, double handle);
    // Ends `brain`'s workout: the flag cleared and the end callback called.
    void endWorkout(Brain& brain);
    // One update of the workouts.
    void updateWorkouts(std::uint64_t nowMs);
    // One update of the players' enemy counts.
    void updateEnemyCounts();
    // One update of the fleeing gangs.
    void updateFlight();
    // The category of `brain`'s human's class, if the configuration names one.
    [[nodiscard]] std::optional<int> categoryOf(const Brain& brain) const;
    // The goals' services, wired to the brains and the lookups.
    void wireServices();

    ScriptedBrains* m_scripted;
    HubLookups m_lookups;
    HubGoalServices m_services;
    std::vector<SaidLine> m_said;
    std::map<double, WorkoutRun> m_workouts;
    std::map<const Brain*, int> m_enemyCounts;
    std::map<const Brain*, std::uint64_t> m_countedAt;
    // The fleeing gangs: their starting count and the standing count last seen.
    struct Flight {
        int start = 0;
        int standing = 0;
    };
    std::map<int, Flight> m_flights;
};

} // namespace coney::ai
