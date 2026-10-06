// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/move_to_flag_goal.h"
#include "ai/script_services.h"
#include "ai/scripted_humans.h"
#include "animation/anim_math.h"
#include "scripting/ai_bindings.h"
#include "warriors/created_humans.h"
#include "world_objects/flag_net.h"
#include "world_objects/flags.h"
#include "world_objects/volume_boxes.h"

namespace coney::script {
class AnimCallbacks;
class MessageHandlers;
class ScriptSystem;
} // namespace coney::script

// The level scripts' hold on brains: which brain each human handle names, and what the AI and gang bindings do with
// it. It gives GoalMoveToFlag its flags (the level's world flags), finds what ActLookAt turns to (a human of a brain, a
// flag, or any object the locator knows), and gives the scripted goals, the gangs and the tactics the script system
// (their callbacks and message handlers) as their ScriptServices.
// Research: docs/research/ai.md#scripted, docs/research/ai.md#gangs

namespace coney::ai {

class ScriptedStory;

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
    /// Sets the objects' message handlers (`SetMsgHandler`) the humans' events and the flags' arrivals go to (null for
    /// none). It must outlive this, or be replaced.
    void setMessages(const script::MessageHandlers* messages) { m_messages = messages; }
    /// Walks `FlagNetTraverse`'s pedestrians on `net` (null: there is none, and they stand); it must outlive them.
    void setFlagNet(const world_objects::FlagNet* net) { m_flagNet = net; }
    /// Sets the scripts' animation callbacks (null for none), which then resolve handles to the bound humans; it must
    /// outlive this, or be replaced. A bound human's anim starts are kept for runAnimCallbacks().
    void setAnimCallbacks(script::AnimCallbacks* callbacks);
    /// Calls the animation callback of each anim a bound human started since the last call, in order, with (human,
    /// anim id). **Coney choice**: the original calls it inside the animation code, as the clip is taken; Coney runs
    /// them after the characters' step, so a callback never changes a human in the middle of its update.
    /// @orig 0x0023ac50 AnimCallback_Dispatch (unknown)
    void runAnimCallbacks();
    /// The bound humans as the volume boxes test them: handle, feet and whether alive (health not run out).
    [[nodiscard]] std::vector<world_objects::BoxSubject> boxSubjects() const;

    /// Makes a human the scripts created (`HuCreate`) a human in the world with a brain, and returns the brain; null
    /// when it makes none (the handle then names no brain).
    using Spawner = std::function<Brain*(const HumanCreation& human)>;
    /// Holds every call but the gangs' creation and counts until release(). **Coney choice**: the original's
    /// `HuCreate` makes the human at once, but Coney runs a level's script before it loads the level's characters, so
    /// the humans the script creates, and what it gives them, wait for the characters (docs/research/ai.md#coney).
    void hold() { m_holding = true; }
    /// From now on a created human is made with `spawner` and bound to its handle (and gang); when holding, the calls
    /// held are then replayed in their order, the creations among them, and the hold ends.
    void release(Spawner spawner);
    /// Takes a deleted human (`HuDelete`) out of the world: its brain, then its human. Empty: nothing is removed.
    using Remover = std::function<void(Brain& brain)>;
    void setRemover(Remover remover) { m_remover = std::move(remover); }
    /// Hands the pad from the player's brain `from` to the brain `to` (`HuSwitchPlayer`), which the play mode does
    /// (ai::AiHumans::switchPlayer()). Empty: no switch is made.
    using Switcher = std::function<void(Brain& from, Brain& to)>;
    /// Sets the switcher, and whether a switch with no team-mate standing may take a human of a kind-0 gang (outside
    /// `level99`, docs/research/rumble.md#switch-player).
    void setSwitcher(Switcher switcher, bool anyKindZero) {
        m_switcher = std::move(switcher);
        m_switchToKindZero = anyKindZero;
    }
    /// Whether calls are being held.
    [[nodiscard]] bool holding() const { return m_holding; }
    /// A human `HuCreate` made while holding, not deleted since: its gang and player index.
    struct HeldHuman {
        int gang = -1;
        int playerIndex = 0;
    };
    /// The human with `handle` when it was created while holding and is not made yet; nothing otherwise. The getters
    /// answer for it as for a standing human (**Coney choice**: Coney runs the start callback before the humans exist).
    [[nodiscard]] std::optional<HeldHuman> heldHuman(double handle) const;
    /// Calls held so far.
    [[nodiscard]] std::size_t held() const { return m_held.size(); }
    /// Keeps `call` for release() while holding and returns true; returns false (the caller runs it now) otherwise.
    bool defer(std::function<void()> call) { return held(std::move(call)); }
    /// The brains it drives, and the bound ones by handle.
    [[nodiscard]] Brains& owner() const { return *m_owner; }
    [[nodiscard]] const std::map<double, Brain*>& bound() const { return m_brains; }
    /// The character bindings' host on the same brains (ai::ScriptedHumans).
    [[nodiscard]] ScriptedHumans& humanHost() { return *m_humans; }
    /// The story bindings' host on the same brains (ai::ScriptedStory).
    [[nodiscard]] ScriptedStory& storyHost() { return *m_story; }
    /// The level's world flags.
    [[nodiscard]] const world_objects::WorldFlags& flags() const { return *m_flags; }
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
    /// Pushes the move-to-human goal (ai/engage_goals.h) on the human's brain.
    void goalMoveToHuman(double human, double target, int gait, float radius) override;
    /// Pushes the engage-enemy goal (ai/engage_goals.h) on the human's brain.
    void goalEngageEnemy(double human, double enemy) override;
    /// The brain's type for 1-6 (Brain::setType()). **Coney stand-in**: 0, the player's type, which hands the human's
    /// player record the pad, is not built, and a value above 6 (past the original's tables) is ignored.
    void brSetType(double human, int type) override;
    /// The brain's weight for one attack kind (Brain::setAttackWeight()), kept to a byte.
    void brSetAttackWeight(double human, int attack, int weight) override;
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
    /// Gives the gang a TacticAttack.
    void tacticAttack(int gang, std::string_view callback) override;
    /// Gives the gang a TacticConfront.
    void tacticConfront(const script::ConfrontCall& call) override;
    /// Gives the gang a TacticDomination over the level's flags.
    void tacticDomination(int gang, double flag, float range, std::string_view callback) override;
    /// Brain::clearActions().
    void brFlushActions(double human) override;
    /// Brain::clearGoals().
    void brFlushGoals(double human) override;
    /// The human's health and its maximum set to `health` (at least 1).
    void setMaxHealth(double human, int health) override;
    /// An AI human: unbound and handed to the remover (setRemover()), which takes it out of the world. **Coney
    /// choice**: a player's human is not deleted.
    void humanDelete(double human) override;
    /// The id of the gang of the brain named by `human`, or of the gang a human created while holding will join.
    [[nodiscard]] std::optional<int> gangOf(double human) const override;
    /// The player's team-mate (switchTarget()) takes the pad through the switcher and becomes player 1 (setPlayer());
    /// its handle, or NilHandle when `human` is not the player's or no one can take over.
    /// @orig 0x0041a8c0 Human_SwitchPlayer (unknown)
    double switchPlayer(double human, bool storyMode) override;
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
    /// Pushes the pedestrian goal on a human that is not a player's.
    /// @orig 0x002a78c8 FlagNet_StartTraverse (unknown)
    void flagNetTraverse(const script::FlagNetTraverseCall& call) override;
    /// The gang's members (only the living with `living`: health left); 0 for no gang. While holding, the humans
    /// created for it count too: **Coney choice**, as Coney runs the start callback before the humans exist.
    [[nodiscard]] int gangHeadCount(int gang, bool living) override;
    /// Gang::standing(), with the humans created for it while holding; 0 for no gang.
    [[nodiscard]] int gangStandingCount(int gang) override;
    /// Makes the human with the spawner and binds its brain to the handle, in `human`'s gang, with its type as the
    /// brain's class; nothing without a spawner.
    void humanCreated(const HumanCreation& human) override;
    /// Moves a bound AI human there, with no ground snap; the player's teleport is the level's (GameplayMode).
    void humanTeleported(double handle, const world_objects::Placement& placement) override;
    /// Where a bound brain's human stands now, and its heading.
    [[nodiscard]] std::optional<world_objects::Placement> humanPlacement(double handle) const override;
    /// The character bindings' host (ai::ScriptedHumans).
    [[nodiscard]] script::HumanBindingHost* humans() override { return m_humans.get(); }
    /// The story bindings' host (ai::ScriptedStory).
    [[nodiscard]] script::StoryBindingHost* story() override;

    // ---- FlagServices ----

    /// The flag with `handle`, where its parent (if live) puts it.
    [[nodiscard]] std::optional<world_objects::Placement> flag(double handle) const override;
    /// Counts the arrival and sends the flag message 8 about `user`: its handler gets `(flag, human)` (the flag's own
    /// handler, `0x00416038`).
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
    /// The human's own handler for the event's number, as message `(human, human, other, value)`
    /// (script::MessageHandlers::deliver()).
    bool humanEvent(Brain& human, const BrainEvent& event) override;
    /// **Coney choice**, with no scene system: the scene is over at once (sceneFinished() is true), and its callback
    /// is scheduled with the human's handle and 1 after kGoalCallbackDelayMs, as a goal's is (what the original's
    /// scene end passes is not traced).
    void playScene(int scene, Brain& human, std::string_view callback) override;

  private:
    // The brain named by `handle`, or null.
    [[nodiscard]] Brain* named(double handle) const { return brain(handle); }
    // The humans created while holding that will join gang `gang` (and were not deleted since): standing and alive.
    [[nodiscard]] int heldMembers(int gang) const;
    // Keeps `call` for release() while holding (true); false when it should run now.
    bool held(std::function<void()> call);
    // Who takes the pad from `from`: a member of its gang in slot order that no pad drives and that is not down (the
    // first found: **Coney's**, as no binding Coney has sets the priority byte `+0x1b1` the original prefers); with
    // none, and when allowed (`anyKindZero`), such a human of a kind-0 gang. Null when there is none.
    // @orig 0x0022a770 Gang_PickSwitchMember (unknown)
    [[nodiscard]] Brain* switchTarget(const Brain& from, bool anyKindZero) const;

    Brains* m_owner;
    const world_objects::WorldFlags* m_flags;
    world_objects::ObjectLocator m_locate;
    script::ScriptSystem* m_scripts = nullptr;
    const script::MessageHandlers* m_messages = nullptr;
    const world_objects::FlagNet* m_flagNet = nullptr;
    script::AnimCallbacks* m_animCallbacks = nullptr;
    std::vector<std::pair<double, std::uint32_t>> m_animStarts; // (human, anim id) since runAnimCallbacks()
    Brain* m_player = nullptr;
    std::map<double, Brain*> m_brains;
    std::size_t m_arrivals = 0;
    Spawner m_spawner;
    Remover m_remover;
    Switcher m_switcher;
    bool m_switchToKindZero = false;
    bool m_holding = false;
    std::vector<std::function<void()>> m_held; // the calls held, oldest first
    std::unique_ptr<ScriptedHumans> m_humans;
    std::unique_ptr<ScriptedStory> m_story;
    std::map<double, HeldHuman> m_heldHumans; // the humans created while holding, by handle
};

} // namespace coney::ai
