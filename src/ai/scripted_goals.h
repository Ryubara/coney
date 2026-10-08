// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "ai/goal.h"
#include "ai/move_to_flag_goal.h"

// Three goals the level scripts give: backing away from a human (`GoalBackoff`, type `0x9b`), a bum's ambient
// behaviour (`GoalBumLogic`, type `0x4f`) and going to a usable world flag to use it (`GoalMoveToUseFlag`, type 4).
// Only their constructors are traced; what they do each update is Coney's, marked on each.
// Research: docs/research/ai.md#goals, docs/references/bindings/ai.md#goalbackoff,
// docs/references/bindings/ai.md#goalbumlogic, docs/references/bindings/ai.md#goalmovetouseflag

namespace coney::ai {

class ScriptServices;

/// `GoalBackoff`'s goal: keep `distance` from another human, for `timeMs` (-1 for no limit) or until `BrClearBackoff`
/// or `BrFlush` ends it. **Coney stand-in** for its process (not traced): while nearer than the distance the human
/// walks straight away from the other, else it stands; it is done once its time is up or the other is gone.
/// @orig 0x002d92e8 BackoffGoal_Init (unknown)
class BackoffGoal final : public Goal {
  public:
    /// Backing away from the human whose handle is `from` (found through `services`, which must outlive it).
    BackoffGoal(ScriptServices& services, double from, float distance, std::int32_t timeMs, bool option)
        : Goal(GoalType::Backoff), m_services(&services), m_from(from), m_distance(distance), m_timeMs(timeMs),
          m_option(option) {}

    /// Notes when it started, for its time.
    void start(Brain& brain) override;
    /// Stops the move.
    void end(Brain& brain) override;
    /// Walks away while too near; done when the time is up or the other is gone.
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    [[nodiscard]] double from() const { return m_from; }
    [[nodiscard]] float distance() const { return m_distance; }
    /// The flag (forced true for player-side humans by the binding's caller; its meaning is inferred).
    [[nodiscard]] bool option() const { return m_option; }

  private:
    ScriptServices* m_services;
    double m_from;
    float m_distance;
    std::int32_t m_timeMs;
    bool m_option;
    std::uint64_t m_startMs = 0;
};

/// `GoalBumLogic`'s arguments.
struct BumOrder {
    std::uint32_t type = 0;   ///< 0, 1 or 2 (begging, rummaging, sleeping: inferred).
    bool option = true;       ///< A flag.
    std::uint32_t chance = 0; ///< Percent, at most 100.
    int value = 3;            ///< A number.
    std::string callback;     ///< A Lua function the goal calls; empty for none.
    bool option2 = true;      ///< A flag.
};

/// `GoalBumLogic`'s goal. **Coney stand-in** for its process (not traced): the bum stands where it is and never ends;
/// the callback is never called.
/// @orig 0x002abef8 BumLogicGoal_Init (unknown)
class BumLogicGoal final : public Goal {
  public:
    explicit BumLogicGoal(BumOrder order) : Goal(GoalType::BumLogic), m_order(std::move(order)) {}
    /// Stands still.
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    [[nodiscard]] const BumOrder& order() const { return m_order; }

  private:
    BumOrder m_order;
};

/// `GoalMoveToUseFlag`'s arguments besides the move.
struct UseFlagOrder {
    float delay = 0.0F;    ///< A float kept with the goal.
    float duration = 0.0F; ///< A float kept with the goal (how long to use the flag, inferred).
    bool reserve = false;  ///< Keep using the flag (inferred).
};

/// `GoalMoveToUseFlag`'s goal: goes to the flag as GoalMoveToFlag does (no offset, turning to the flag's heading) and
/// then uses it. The flag is reserved for the human from the goal's making until its end (`0x004161b0`).
/// **Coney stand-in** for the use (the flags' use clips are not researched): once there the human stands at the flag
/// and the goal never ends.
/// @orig 0x002db768 MoveToUseFlagGoal_Init (unknown)
class MoveToUseFlagGoal final : public Goal {
  public:
    /// Going as `move` says (through `services`, which must outlive it), then using the flag as `use` says; `release`
    /// (may be empty) is called at its end to free the flag's reservation.
    MoveToUseFlagGoal(const MoveToFlagOrder& move, FlagServices& services, const UseFlagOrder& use,
                      std::function<void()> release);
    /// The move's start.
    void start(Brain& brain) override;
    /// The move's resume.
    void resume(Brain& brain) override;
    /// Frees the flag.
    void end(Brain& brain) override;
    /// The move until it arrives, then the use.
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// Whether it reached the flag.
    [[nodiscard]] bool inUse() const { return m_arrived; }
    [[nodiscard]] const UseFlagOrder& use() const { return m_use; }

  private:
    std::unique_ptr<MoveToFlagGoal> m_move;
    UseFlagOrder m_use;
    std::function<void()> m_release;
    bool m_arrived = false;
};

/// What the tag goal asks of the level.
struct TagServices {
    /// Whether a human other than `human` holds the flag `flag`.
    std::function<bool(double flag, double human)> heldByOther;
    /// The spray's start on `tag` from `flag` (`Human_Tag`, as `HuTag` starts it).
    std::function<void(double human, double tag, double flag)> startTag;
    /// Whether `human` is spraying a tag now.
    std::function<bool(double human)> tagging;
    /// Message `0x19` with 4 to the tag object `tag`: blank, to be painted in.
    std::function<void(double tag)> blank;
};

/// The tag goal's arrival radius, m: farther than this once stopped, it gives up.
inline constexpr float kTagReach = 1.5F;

/// `GoalTag`'s goal (type `0x5a`, docs/research/ai-goals.md#goal-tag): the human runs (gait 4) to the flag; within
/// kTagReach the other tag object, if any, is made blank once and the spray starts on the tag; it is done once the
/// spray is over. It is also done when the flag is gone, the human is cuffed or down, another human holds the flag, or
/// the run stopped more than kTagReach short. The flag is claimed from the goal's making until its end.
/// **Coney's reading**: the start of the spray is the Tag action's start (`Human_Tag`) without the action's command
/// presses, and "down" is the human not alive (Human::alive()).
/// @orig 0x002ccfe0 TagGoal_Init (unknown)
/// @orig 0x002cd258 TagGoal_Process (unknown)
class TagGoal final : public Goal {
  public:
    /// Spraying `tag` from `flag`, made blank first `extra` (0 for none); `flags` must outlive it, and `release` (may
    /// be empty) frees the flag's claim at its end.
    TagGoal(double flag, double tag, double extra, FlagServices& flags, TagServices services,
            std::function<void()> release);
    /// The run's start.
    void start(Brain& brain) override;
    /// The run's resume while it runs.
    void resume(Brain& brain) override;
    /// Frees the flag.
    /// @orig 0x002cd198 TagGoal_Suspend (unknown)
    void end(Brain& brain) override;
    /// The three states: run, arrive and start, spray.
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The state (`+0x24`): 0 running, 1 arrived, 2 spraying.
    [[nodiscard]] int state() const { return m_state; }

  private:
    std::unique_ptr<MoveToFlagGoal> m_move;
    FlagServices* m_flags;
    TagServices m_services;
    std::function<void()> m_release;
    double m_flag;
    double m_tag;
    double m_extra;
    int m_state = 0;
};

} // namespace coney::ai
