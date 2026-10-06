// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ai/goal.h"
#include "animation/anim_math.h"

// The goals the hub (`level95`) gives its people: a stroll round a centre (the police searching a crime scene among
// them), a boxer working a target, a human holding another, a vendor beckoning passers-by, one generic clip, a
// shopkeeper minding his store, and the flight a gang's beaten members take. Each is driven by its brain as the other
// goals are (ai/goal.h); what they ask of the level beyond the brain (where things are, the store's box and flags, the
// crimes, the people about, the scripts) comes in as HubGoalServices, which the level's scripted brains fill.
// Research: docs/references/bindings/ai.md, docs/references/bindings/gang.md#gangcanflee

namespace coney::ai {

class Brain;
class ScriptServices;

/// What the hub's goals ask of the level. Each function may be empty: the goal then does without it.
struct HubGoalServices {
    /// Where the object with a handle (a flag, a human) is; nothing once it is gone.
    std::function<std::optional<anim::Vec3>(double handle)> locate;
    /// Where the `CrimeScene` flag is; nothing without one.
    std::function<std::optional<anim::Vec3>()> crimeScene;
    /// Every brain about (the candidates a vendor beckons, the store's visitors), player 1's among them.
    std::function<std::vector<Brain*>()> brains;
    /// Player 1's brain; null for none.
    std::function<Brain*()> player;
    /// Whether a point is inside a volume box (by its handle).
    std::function<bool(double box, anim::Vec3 point)> inBox;
    /// The positions of the world flags inside a volume box.
    std::function<std::vector<anim::Vec3>(double box)> flagsInBox;
    /// How many crimes have been reported so far, and where the last was: a goal compares the count with the one it
    /// saw.
    std::function<std::uint64_t()> crimeCount;
    std::function<std::optional<anim::Vec3>()> lastCrime;
    /// Reports a break-in (crime type 1) at a point.
    std::function<void(anim::Vec3 at)> reportBreakIn;
    /// The scripts the callbacks run through; null runs none.
    ScriptServices* scripts = nullptr;
};

/// A line a goal has a human say, by the name the page gives it (`beckon`, `store_greet`...). **Coney stand-in**: which
/// sound each name plays is not on the page, so the goals keep the lines they said (for the tests) and play nothing.
struct SaidLine {
    double human = 0;
    std::string line;
};

/// `GoalAreaWalker`'s goal (type 71): the human strolls round a centre (a flag, or where it stood), walking (gait 2) to
/// random points within half the radius and pausing a random 0 to `pauseSeconds` s at each, until `durationSeconds`
/// pass (0: for ever); below a radius of 1 it stands at the centre facing its first heading. Mode 3 (the police) also
/// idles at each stop (anim `0x29c`, or `0x29e` one time in four) and follows the `CrimeScene` flag, running there when
/// it moved more than 5 m. A point the move cannot reach is given up for another. **Coney choice**: the points are
/// drawn uniformly over the disc's area.
/// @orig 0x002a4dc8 AreaWalkerGoal_Init (unknown)
class AreaWalkerGoal final : public Goal {
  public:
    /// Round `flag` (0: where the human stands at the start) with `radius` m in `mode`, for `durationSeconds`, pausing
    /// up to `pauseSeconds`, through `services`.
    AreaWalkerGoal(double flag, int radius, int mode, std::uint32_t durationSeconds, int pauseSeconds,
                   const HubGoalServices& services);
    /// The centre, the first heading and the end time.
    void start(Brain& brain) override;
    /// Clears the actions.
    void resume(Brain& brain) override;
    /// The walk, the pause and the crime scene (above).
    /// @orig 0x002a4eb0 AreaWalkerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The centre it strolls round.
    [[nodiscard]] anim::Vec3 centre() const { return m_centre; }

  private:
    double m_flag;
    int m_radius;
    int m_mode;
    std::uint32_t m_durationSeconds;
    int m_pauseSeconds;
    const HubGoalServices* m_services;
    anim::Vec3 m_centre;
    float m_firstHeading = 0.0F;
    std::uint64_t m_endMs = 0;
    std::uint64_t m_pauseUntilMs = 0;
    bool m_walking = false;
};

/// `GoalBoxer`'s goal (type 158): the boxer fights `target` for ever. While the target cannot be fought he waits; out
/// of reach he waits 2 s three times in four, else walks up to it; in reach he takes it as his target and fights
/// (Brain::fight(), the fight goal pushed over this one), and when that fight ends this one takes over again.
/// **Coney stand-ins**: the boxing attack weights (`0x00511120`) are not on the page, so he fights with his own; the
/// dance, the held blocks and the pauses between attacks are the fight goal's.
/// @orig 0x002d9848 BoxerGoal_Init (unknown)
class BoxerGoal final : public Goal {
  public:
    /// Boxing `target`, through `services`.
    BoxerGoal(double target, const HubGoalServices& services);
    /// The wait, the walk or the fight (above).
    /// @orig 0x002d98f8 BoxerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

  private:
    double m_target;
    const HubGoalServices* m_services;
    std::uint64_t m_waitUntilMs = 0;
};

/// `GoalGrabTarget`'s goal (type 31): the grabber walks up to the target and holds it, turning to face it, until the
/// target is gone; meanwhile he cannot be targeted and never fights on his own (threat response 0), and his End gives
/// the targeting back (the threat response stays 0, as the original's End leaves it). **Coney stand-ins**: Coney's AI
/// humans make no grab yet and the hold's damage (`0x00510acc`) is not on the page, so the hold is the grabber
/// standing at the target, facing it, and dealing nothing.
/// @orig 0x002bb458 GrabTargetGoal_Init (unknown)
class GrabTargetGoal final : public Goal {
  public:
    /// Holding `target`, through `services`.
    GrabTargetGoal(double target, const HubGoalServices& services);
    /// Untargetable and peaceful.
    /// @orig 0x002bb4c0 GrabTargetGoal_Start (unknown)
    void start(Brain& brain) override;
    /// The walk, then the hold; done once the target is gone.
    /// @orig 0x002bb858 GrabTargetGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Targetable again.
    /// @orig 0x002bb598 GrabTargetGoal_End (unknown)
    void end(Brain& brain) override;

    /// Whether he has reached the target and holds it.
    [[nodiscard]] bool holding() const { return m_holding; }

  private:
    double m_target;
    const HubGoalServices* m_services;
    bool m_holding = false;
};

/// `GoalPeddler`'s goal (type 80): the vendor holds the spot and heading he had (walking back beyond 0.3 m, turning
/// back beyond 15°) and never fights (threat response 0, restored at the end). Every 20 brain updates he picks the
/// nearest passer-by within `range` he has not beckoned and beckons him once: the greet clip (anim 604) and
/// `beckon_player` (a player, always) or `beckon` (anyone else, half the time), looking at him for the beckon. With
/// `reacts` a civilian vendor who is hurt flees from the nearest human (the pedestrian reaction,
/// PedestrianReactionGoal). **Coney stand-in**: the clips are only bound when both names are given, and the look lasts
/// kBeckonLookMs (the clip's length is not read).
/// @orig 0x002ad378 PeddlerGoal_Init (unknown)
class PeddlerGoal final : public Goal {
  public:
    /// How long he looks at someone he beckons, ms (**Coney stand-in** for the greet clip's length).
    static constexpr std::uint64_t kBeckonLookMs = 2000;

    /// A vendor beckoning within `range`, reacting to harm with `reacts`, with the clips `greetAnim` and `idleAnim`;
    /// the lines said go to `said` (which must outlive the goal).
    PeddlerGoal(float range, bool reacts, const std::string& greetAnim, const std::string& idleAnim,
                const HubGoalServices& services, std::vector<SaidLine>& said);
    /// The spot, the heading and the threat response.
    void start(Brain& brain) override;
    /// The spot, the heading, the beckon (above).
    /// @orig 0x002ad6c0 PeddlerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// The threat response back.
    /// @orig 0x002ad530 PeddlerGoal_End (unknown)
    void end(Brain& brain) override;

  private:
    float m_range;
    bool m_reacts;
    bool m_clips;
    const HubGoalServices* m_services;
    std::vector<SaidLine>* m_said;
    anim::Vec3 m_spot;
    float m_heading = 0.0F;
    int m_savedThreat = 0;
    int m_lastHealth = -1;
    std::vector<const Brain*> m_beckoned;
    std::uint64_t m_lookUntilMs = 0;
};

/// `GoalPlayGenAnim`'s goal (type 38): one generic clip, 0 the shoulder charge (anim 0) or 1 the camera flash (anim
/// 665), played once the human has no queued action; done once that action has left the queue, or at once for any
/// other number. Its end, also when cut short, schedules the callback with the human's handle 33 ms later.
/// @orig 0x002d46b8 PlayGenAnimGoal_Init (unknown)
class PlayGenAnimGoal final : public Goal {
  public:
    /// The shoulder charge's and the camera flash's anim ids.
    static constexpr int kChargeAnim = 0;
    static constexpr int kFlashAnim = 665;
    /// The callback's delay after the end, ms.
    static constexpr std::uint32_t kCallbackDelayMs = 33;

    /// Clip `anim` (0 or 1), then `callback` (empty for none), through `services`.
    PlayGenAnimGoal(int anim, std::string callback, ScriptServices* services);
    /// @orig 0x002d47d0 PlayGenAnimGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Schedules the callback.
    /// @orig 0x002d4748 PlayGenAnimGoal_End (unknown)
    void end(Brain& brain) override;

  private:
    int m_anim;
    std::string m_callback;
    ScriptServices* m_services;
    bool m_queued = false;
};

/// `GoalShopkeeper`'s goal (type 130): the shopkeeper minds his store (a volume box). His brain becomes type 6 with a
/// threat response of 0. He idles at his counter (where he stood), every 3-5 s walks to a flag inside the store (and
/// back), greets the player with `store_greet` when he comes within half the range (at least 4 m) and chats
/// (`store_chat`) every 10-20 s while he stays; a player beyond the range sends him back into the store. The first
/// time a crime happens in the store (a crime reported inside the box, or harm to himself) he calls `onDisturbed` and
/// reacts by kind: 1 phones (`phone_gang`), then calls `onPhone` or, without it, reports a break-in (crime type 1);
/// 2 fights the offender (`dead_meat`); 3 and 4 cower (`cower`), and a kind-3 shopkeeper who pleads gives in once
/// below 40 % health (`mug_grunt`). **Coney choices**: kind 0 picks 2 seven times in ten, else 3; the offender is
/// player 1; the callbacks get the shopkeeper's handle. **Coney stand-ins**: the counter clips, the broom and the
/// pleading's context action are not built.
/// @orig 0x002e5ae0 ShopkeeperGoal_Init (unknown)
class ShopkeeperGoal final : public Goal {
  public:
    /// What he does now.
    enum class Mood : std::uint8_t { Minding, Phoning, Fighting, Cowering, GivenIn };

    /// Minding `store` as `kind`, with a broom (`broom`, not for kind 1), within `range`; the callbacks may be empty.
    ShopkeeperGoal(double store, int kind, bool broom, float range, std::string onDisturbed, std::string onPhone,
                   bool pleads, const HubGoalServices& services, std::vector<SaidLine>& said);
    /// The brain type, the threat response, the kind and the counter.
    void start(Brain& brain) override;
    /// The minding and the reactions (above).
    /// @orig 0x002e6668 ShopkeeperGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The kind after the start's pick, and what he does now.
    [[nodiscard]] int kind() const { return m_kind; }
    [[nodiscard]] Mood mood() const { return m_mood; }
    [[nodiscard]] bool broom() const { return m_broom; }

  private:
    // The first crime in the store: the callback, then the kind's reaction.
    void disturbed(Brain& brain);
    // Has the human say `line`.
    void say(const Brain& brain, std::string_view line);

    double m_store;
    int m_kind;
    bool m_broom;
    float m_range;
    std::string m_onDisturbed;
    std::string m_onPhone;
    bool m_pleads;
    const HubGoalServices* m_services;
    std::vector<SaidLine>* m_said;
    Mood m_mood = Mood::Minding;
    anim::Vec3 m_counter;
    std::uint64_t m_nextWanderMs = 0;
    std::uint64_t m_nextChatMs = 0;
    bool m_greeted = false;
    bool m_away = false;
    std::uint64_t m_crimesSeen = 0;
    int m_lastHealth = -1;
};

/// The pedestrian reaction goal (type `0x6b`) in mode 9, the flight: the human runs (gait 4) away from a point.
/// **Coney stand-in**: the goal's other modes and its end are not on the page; the flight lasts kFleeMs, aiming each
/// time kFleeStep m further from the point.
class PedestrianReactionGoal final : public Goal {
  public:
    /// How long a flight lasts, ms, and how far each leg goes, m.
    static constexpr std::uint64_t kFleeMs = 10000;
    static constexpr float kFleeStep = 10.0F;

    /// Fleeing from `from`.
    explicit PedestrianReactionGoal(anim::Vec3 from) : Goal(GoalType::PedestrianReaction), m_from(from) {}
    /// The flight's end time.
    void start(Brain& brain) override;
    /// One leg at a time, away from the point.
    [[nodiscard]] GoalStatus process(Brain& brain) override;

  private:
    anim::Vec3 m_from;
    std::uint64_t m_untilMs = 0;
};

} // namespace coney::ai
