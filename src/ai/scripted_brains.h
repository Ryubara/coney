// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/move_to_flag_goal.h"
#include "ai/script_services.h"
#include "animation/anim_math.h"
#include "scripting/ai_bindings.h"
#include "world_objects/flags.h"

namespace coney::script {
class ScriptSystem;
} // namespace coney::script

// The level scripts' hold on brains: which brain each human handle names, and what the AI and gang bindings do with
// it. It gives GoalMoveToFlag its flags (the level's world flags), finds what ActLookAt turns to (a human of a brain, a
// flag, or any object the locator knows), and gives the scripted goals, the gangs and the tactics the script system
// (their callbacks and message handlers) as their ScriptServices.
// Research: docs/research/ai.md#scripted, docs/research/ai.md#gangs

namespace coney::ai {

/// The brains the scripts drive by handle, and the gangs they make.
class ScriptedBrains final : public script::AiBindingHost, public FlagServices, public ScriptServices {
  public:
    /// The scripts' hold on `brains` (their gangs and formations), with `flags` the level's world flags and `locate`
    /// (may be empty) for other objects; all must outlive it. It becomes the gangs' script services.
    ScriptedBrains(Brains& brains, const world_objects::WorldFlags& flags, world_objects::ObjectLocator locate = {});
    ScriptedBrains(const ScriptedBrains&) = delete;
    ScriptedBrains& operator=(const ScriptedBrains&) = delete;
    ScriptedBrains(ScriptedBrains&&) = delete;
    ScriptedBrains& operator=(ScriptedBrains&&) = delete;
    /// Takes itself out of the gangs.
    ~ScriptedBrains() override;

    /// Sets the script system the callbacks and handlers run in (null for none: they are then dropped). It must
    /// outlive this, or be replaced.
    void setScripts(script::ScriptSystem* scripts) { m_scripts = scripts; }
    /// Names `brain` (which must outlive the binding, or be unbound first) by `handle`, sets its handle, and puts it in
    /// gang `gang` (`HuCreate`'s seventh argument; -1 for none).
    void bind(double handle, Brain& brain, int gang = -1);
    /// Forgets the brain named by `handle`.
    void unbind(double handle) { m_brains.erase(handle); }
    /// Sets player 1's brain (null for none).
    void setPlayer(Brain* player) { m_player = player; }
    /// Where the object with `handle` is: a bound brain's human, a flag, or what the locator finds; nothing when
    /// none is.
    [[nodiscard]] std::optional<anim::Vec3> locate(double handle) const;

    // ---- script::AiBindingHost: each does nothing for a handle with no brain or an id with no gang ----

    /// Pushes the move-to-flag goal on the human's brain.
    void goalMoveToFlag(const script::MoveToFlagCall& call) override;
    /// Queues the look-at on the human's brain, its target found by locate().
    /// @orig 0x002fe0c8 Action_LookAt (unknown)
    void actLookAt(const script::LookAtCall& call) override;
    /// The human starts a fight with the target (Brain::startFight()).
    void goalFight(double human, double target) override;
    /// Brain::flush(): the goals end (their callbacks fire), then the actions.
    void brFlush(double human) override;
    /// Brain::setDead().
    void brDead(double human, bool dead) override;
    /// The actions cleared, then the brain suspended or resumed.
    void brSuspend(double human, bool suspended) override;
    /// Brain::setThreatResponse().
    void brSetThreatResponse(double human, int response) override;
    /// goalPlayDynAnimation().
    void goalPlayDynAnimation(const script::DynAnimationCall& call) override;
    /// goalAddressPerson().
    void goalAddressPerson(const script::AddressPersonCall& call) override;
    /// goalTrackHuman().
    void goalTrackHuman(double human, double target, float distance) override;
    /// Pushes a DealerGoal of dealerTypeFor() the brain's class and the call's type.
    void goalDealer(const script::DealerCall& call) override;
    /// The leader's formation (made on first use): its slot count, a slot, the set in use.
    void brSetNumFollowSlots(double leader, int count, int allowed) override;
    void brSetFollowSlot(double leader, int slot, float x, float y, int set) override;
    void brSetFollowSlotSet(double leader, int set) override;
    /// Gives the gang a TacticCrowd.
    void tacticCrowd(int gang, std::string_view callback, bool cheering) override;
    /// TacticCrowd::trigger() when the gang's tactic is a crowd; nothing for another tactic.
    void tacticTrigger(int gang, int what, bool on) override;
    /// Ends and drops the gang's tactic.
    void tacticClear(int gang) override;
    /// The gang calls: Gangs' members of the same names.
    [[nodiscard]] int gangCreate(int kind, std::string_view name) override;
    void gangDelete(int gang) override;
    void gangAddMember(int gang, double human) override;
    void gangBrDead(int gang, bool dead) override;
    void gangBrFlush(int gang) override;
    void gangSetThreatResponse(int gang, int response) override;
    void gangMakeEnemies(int a, int b) override;
    void gangMakeFriends(int a, int b) override;
    void gangSetMsgHandler(int gang, int message, std::string_view handler) override;
    void gangSuspend(int gang, bool suspended) override;
    /// The gang's members (only the living with `living`: health left); 0 for no gang.
    [[nodiscard]] int gangHeadCount(int gang, bool living) override;
    /// Gang::standing(); 0 for no gang.
    [[nodiscard]] int gangStandingCount(int gang) override;

    // ---- FlagServices ----

    /// The flag with `handle`, where its parent (if live) puts it.
    [[nodiscard]] std::optional<world_objects::Placement> flag(double handle) const override;
    /// Counts the arrival: Coney's flags take no message 8 yet.
    void arrived(double handle, Brain& user) override;
    /// Arrivals at flags so far.
    [[nodiscard]] std::size_t arrivals() const { return m_arrivals; }

    // ---- ScriptServices ----

    /// The brain named by `handle`; null when none.
    [[nodiscard]] Brain* brain(double handle) const override;
    /// Player 1's brain, as setPlayer() gave it.
    [[nodiscard]] Brain* player() const override { return m_player; }
    /// ScriptSystem::schedule() when there is a script system.
    void schedule(std::string_view function, std::span<const double> args, std::uint32_t delayMs) override;
    /// ScriptSystem::call() when there is a script system. **Coney choice**: the result is whether the call ran, as
    /// Coney's script system does not hand back what the function returned.
    bool call(std::string_view function, std::span<const double> args) override;
    /// **Coney choice**, with no scene system: the scene is over at once (sceneFinished() is true), and its callback
    /// is scheduled with the human's handle and 1 after kGoalCallbackDelayMs, as a goal's is (what the original's
    /// scene end passes is not traced).
    void playScene(int scene, Brain& human, std::string_view callback) override;

  private:
    // The brain named by `handle`, or null.
    [[nodiscard]] Brain* named(double handle) const { return brain(handle); }

    Brains* m_owner;
    const world_objects::WorldFlags* m_flags;
    world_objects::ObjectLocator m_locate;
    script::ScriptSystem* m_scripts = nullptr;
    Brain* m_player = nullptr;
    std::map<double, Brain*> m_brains;
    std::size_t m_arrivals = 0;
};

} // namespace coney::ai
