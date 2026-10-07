// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/riot_goals.h"
#include "animation/anim_math.h"
#include "scripting/story_bindings.h"

// The level scripts' hold on the humans, brains, gangs and paths for the story missions' bindings
// (scripting/story_bindings.h): the humans the scripts name by handle (ai::ScriptedBrains) are killed, healed and
// switched; their brains get the exit, path, enemy, throw and idle goals and their senses; the gangs their turf,
// leader, ranges, exits and the Warrior commands' crews. Like the other bindings, a call on a human not made yet waits
// until the level makes it (ScriptedBrains::hold()). After each characters' step update() lets the leaving gangs go.
// Research: docs/references/bindings/story.md, docs/research/ai.md#warrior-commands

namespace coney::world_objects {
class VolumeBoxes;
} // namespace coney::world_objects

namespace coney::ai {

class Brain;
class Gang;
class ScriptedBrains;

/// Whether `point` lies in one of `gang`'s turf boxes among `boxes`; no gang, no boxes or a gang with no turf takes
/// any point.
/// @orig 0x001652e8 Gang_IsPointInTurf (unknown)
[[nodiscard]] bool pointInTurf(const Gang* gang, const world_objects::VolumeBoxes* boxes, anim::Vec3 point);

/// A level's paths hold at most this many (`0x006fd870`, 32 slots).
inline constexpr std::size_t kPathSlots = 32;
/// One path `AddPath` made: its name and its points (flag handles), in order.
struct WorldPath {
    std::string name;
    std::vector<double> points;
};

/// The story bindings' host on the brains a level's scripts drive.
class ScriptedStory final : public script::StoryBindingHost {
  public:
    /// The hold on `scripted`'s brains (which must outlive it).
    explicit ScriptedStory(ScriptedBrains& scripted) : m_scripted(&scripted) {}

    /// After each characters' step: each leaving gang (`GangExitWorld`) with no living member left calls its callback
    /// with its id, and is deleted unless kept.
    /// @orig 0x0016a1e8 Gang_FreeWhenEmpty (unknown)
    void update();

    // ---- script::StoryBindingHost: each does nothing for a handle with no human or an id with no gang ----

    /// A bound human's feet, a flag, or what the scripted brains' locator finds.
    [[nodiscard]] std::optional<script::StoryPoint> position(double handle) const override;
    /// The straight distance when the straight line is walkable (or the level has no path data), else the route's
    /// length from the start through its nodes to the end.
    [[nodiscard]] std::optional<float> walkingDistance(double from, double to) override;
    /// Keeps the path; the zero handles and those that name no flag are skipped.
    bool addPath(double handle, std::string_view name, const std::array<double, 8>& points) override;
    /// The path `handle` names; null for none.
    [[nodiscard]] const WorldPath* path(double handle) const;

    /// Health 1 and a 100-point hit pending, which the next update applies as the usual damage. Nothing for a human
    /// out of health already. **Coney stand-in**: Coney's humans have no hat to knock off.
    void killHuman(double human) override;
    /// Clamped to its maximum.
    void setHealth(double human, int health) override;
    void setShadow(double human, bool on) override;
    void lockMovement(double human, bool locked) override;
    /// **Coney's reading** of the handlers by state: an AI human (or a player's dead brain) `aiControl`; a player held
    /// `grabbedControl`, holding someone `grabbingControl`, in the air `jumpingControl`, locked on `lockOnControl`,
    /// otherwise `screenRelativeControl`.
    [[nodiscard]] std::optional<std::string> controlName(double human) const override;
    /// **Coney stand-in**: Coney has no throw aim, so no object is ever aimed at.
    [[nodiscard]] bool aimingAt(double human, double target) const override;
    [[nodiscard]] std::string heldObject(double human) const override;
    /// Held in another's grab or tackle.
    [[nodiscard]] bool grabbed(double human) const override;
    /// **Coney's reading** of the state mask: held, holding someone, down, in the air or climbing, or out of health.
    [[nodiscard]] bool actionsBlocked(double human) const override;
    void setSightRange(double human, float range) override;
    /// Stored in radians as the original stores it.
    void setFieldOfView(double human, float degrees) override;
    void setInvestigateResponse(double human, int response) override;
    void setReactToViolence(double human, bool reacts) override;
    void setPedType(double human, std::uint16_t type) override;
    /// Human::setWounded(), and on wounding the brain flushed (Brain::flush()).
    void setWounded(double human, bool wounded) override;
    void setTagColour(double human, std::uint32_t rgba) override;
    /// What `HuTag` starts: the human, the tag spot and the flag he sprays from.
    using TagHandler = std::function<void(double human, double tag, double flag)>;
    /// The tagging `HuTag` hands its calls to (the play mode's, which runs the player's stick game).
    void setTagHandler(TagHandler handler) { m_tagHandler = std::move(handler); }
    /// Hands the call to the tag handler; without one nothing happens.
    void tag(double human, double tag, double flag) override;
    /// The level's volume boxes, where the gangs' turf boxes are found (null for none: every point is in turf).
    void setBoxes(const world_objects::VolumeBoxes* boxes) { m_boxes = boxes; }
    /// Kept on the human (ScriptState); true when the handle names a human (or calls are held for the level).
    bool setInterrogation(double human, const std::array<std::string, 4>& lines, std::string_view callback,
                          bool icon) override;
    void applyDamageModifier(double human, float factor) override;
    void makeEnemiesOfType(int gang, int kind) override;
    void setAlwaysSeen(int gang, bool on) override;

    /// The brain's off flag set (as `BrDead`) unless it is a player's, then the exit goal; flag 0 takes the nearest
    /// exit flag (`HuExitWorld`), and with none nothing happens.
    void goalMoveToExitFlag(const script::ExitFlagCall& call) override;
    void goalTravelPath(const script::TravelPathCall& call) override;
    /// A FindEnemyGoal, and over it a fight with the target when one is named (Brain::fight(); **Coney stand-in** for
    /// the melee goal 8 with its 4000 ms). Nothing for a human down or out of health.
    void goalMelee(double human, double target) override;
    void goalThrowObject(const script::ThrowObjectCall& call) override;
    /// RiotGoal over the human's goals (the leaving through ScriptedStory's exits).
    void goalRiot(const script::RiotCall& call) override;
    /// StationaryThrowerGoal over the human's goals.
    void goalStationaryThrower(const script::StationaryThrowerCall& call) override;
    void goalPlayDynIdle(const script::DynIdleCall& call) override;
    /// `GoalGuardFlag`: a GuardFlagGoal pushed.
    void goalGuardFlag(const script::GuardFlagCall& call) override;
    /// `GoalLeadChase`: a LeadChaseGoal along the path pushed.
    void goalLeadChase(const script::LeadChaseCall& call) override;
    /// `GoalDevilRun`: a DevilRunGoal along the path pushed.
    void goalDevilRun(const script::DevilRunCall& call) override;
    /// `GoalBigLedgeThrower`: a BigLedgeThrowerGoal pushed. **Coney stand-in**: the anim name is not applied.
    void goalBigLedgeThrower(const script::LedgeThrowerCall& call) override;
    /// A bum in its bum goal plays its reaction clip (anim 668 for types 1 and 2, 669 for type 0).
    void bumTrigger(double human) override;

    /// Into the first free of the 8 turf slots; a ninth is ignored.
    void addTurfBox(int gang, double box) override;
    void removeTurfBox(int gang, double box) override;
    /// Every AI member fights the target.
    void engageEnemy(int gang, double target) override;
    void setGangInvestigateResponse(int gang, int response) override;
    void setRespondPercentage(int gang, int percent) override;
    void setHearRange(int gang, bool help, float range) override;
    void enableAttackStrategies(int gang, bool on) override;
    void setLeader(int gang, double human) override;
    [[nodiscard]] double leader(int gang) const override;
    /// Each AI member walks to the exit (0: the nearest exit flag to it) and leaves; update() lets the gang go once it
    /// is empty. An empty gang calls back and goes at once.
    void gangExitWorld(int gang, double exit, std::string_view callback, bool deleteGang) override;
    /// Switches the gang's spawner (ScriptedHumans::spawners(), Spawners::start()).
    void startSpawner(int gang, std::string_view name, int mode, int value) override;
    void setSpawnerMaxConcurrent(int gang, std::string_view name, int count) override;
    void setSpawnerOffScreen(int gang, std::string_view name, bool on) override;
    void canUseWorldFlags(int gang, bool on, int percent) override;
    /// The gang takes the story tactic of the call's kind (ai/story_tactics.h); a TravelPath walks the path the call
    /// names. Attack and Confront are the AI host's (ScriptedBrains::tacticAttack(), tacticConfront()).
    void setTactic(const script::TacticCall& call) override;

    /// **Coney stand-in** for the commands' tactics (not traced): the crew's tactic is cleared and its AI members
    /// flushed, then 0 follow and 2 defend have them track the chief, 1 attack look for enemies to fight, 3 hold
    /// stand where they are; 4, 5 and 6 start nothing more.
    bool startWarriorCommand(double chief, int command, bool forced) override;
    [[nodiscard]] double playerOne() const override;

    /// The system music's mood round player 1: 1 (fight) while an AI human with health left targets him and has a fight
    /// or melee goal, else 0 (calm). **Coney stand-in**: the hunted mood (2, a gang member chasing him, goals `0xc`,
    /// `0x75`, `0x76`) is not built, as those goals are not.
    /// @orig 0x001696e0 Gangs_MusicMood (unknown)
    [[nodiscard]] int musicMood() const;
    /// The last Warrior command started (-1 for none).
    [[nodiscard]] int warriorCommand() const { return m_warriorCommand; }

  private:
    // Runs `body` on the brain named by `handle` now, or when the level makes it while calls are held; nothing when
    // no brain has the handle.
    void onBrain(double handle, const std::function<void(Brain&)>& body);
    // Runs `body` on the gang with `id` now, or once the calls held are replayed; nothing when no gang has it.
    void onGang(int id, const std::function<void(Gang&)>& body);
    // The nearest enabled exit flag to `from` other than `exclude` (and, given `accept`, whose position it accepts).
    [[nodiscard]] std::optional<double> nearestExit(anim::Vec3 from, double exclude,
                                                    const std::function<bool(anim::Vec3)>& accept = {}) const;
    // Fills m_riot: the riot goal's services on this level.
    void makeRiotServices();
    // Pushes the exit goal toward `flag` on `brain`.
    void leave(Brain& brain, double flag, int gait, float angle, float distance, float radius);

    ScriptedBrains* m_scripted;
    const world_objects::VolumeBoxes* m_boxes = nullptr;
    std::map<double, WorldPath> m_paths;
    RiotServices m_riot; // what every rioter asks of the level (set on the first GoalRiot)
    int m_warriorCommand = -1;
    TagHandler m_tagHandler;
};

} // namespace coney::ai
