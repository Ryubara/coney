// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "animation/anim_pose.h"
#include "animation/anim_task.h"
#include "characters/anim_set.h"
#include "human/locomotion.h"

// A human's animation controller: each update it picks an anim state from what the human is doing and, when the state
// changes, builds the animation tasks for it; while moving it keeps the gait blend's target on the speed.
// Research: docs/research/characters.md#clip-selection

namespace coney::human {

/// The anim slots a human plays its locomotion from, by slot number (docs/research/characters.md#anim-slots).
struct AnimSlots {
    static constexpr std::size_t kCount = 35;
    std::array<std::uint32_t, kCount> ids{};

    /// The player's slots as read from Rembrandt at runtime: the default table's locomotion ids, with slot 14 = 380.
    /// Only the slots Coney uses are filled; the rest are 0.
    [[nodiscard]] static AnimSlots player();
};

/// Slot numbers the locomotion uses.
inline constexpr std::size_t kSlotIdle = 0;
inline constexpr std::size_t kSlotSneak = 3;
inline constexpr std::size_t kSlotWalk = 4;
inline constexpr std::size_t kSlotJog = 5;
inline constexpr std::size_t kSlotRun = 6;
inline constexpr std::size_t kSlotSprint = 7;
inline constexpr std::size_t kSlotWalkStart = 10;
inline constexpr std::size_t kSlotCombatWalk = 14;
inline constexpr std::size_t kSlotDropCycle = 26;

/// Anim states, by the original's numbers where it has them.
enum class AnimState : std::int8_t {
    None = -1, ///< Nothing built yet.
    Idle = 0,
    Move = 4,
    Fall = 100, ///< **Coney's**: the airborne state's builder is not researched; Coney plays the drop cycle.
};

/// What the controller decides from, each update.
struct AnimInputs {
    float speed = 0.0F;     ///< The human's horizontal speed, m/s.
    bool wantsMove = false; ///< The stick asks for movement (above the dead zone).
    bool wantsRun = false;  ///< The stick asks for a run (above kRunThreshold).
    bool airborne = false;  ///< The human is falling.
};

/// The fade lengths of the builders, seconds.
inline constexpr float kIdleFade = 0.15F;
inline constexpr float kIdleFadeEarlyStart = 1.0F / 15.0F;
inline constexpr float kIdleFadeLateStart = 0.2F;
inline constexpr float kStartClipEarly = 0.1333F;
inline constexpr float kMoveFadeMoving = 0.1333F;
/// The gait blend's value speed, units a second, for the locomotion.
inline constexpr float kGaitValueSpeed = 10.0F;

/// The human's speeds from its anim set and slots: each slot's clip's root motion.
/// @orig 0x00254078 Human_ComputeSpeeds (unknown)
[[nodiscard]] Speeds speedsOf(const characters::AnimSet& anims, const AnimSlots& slots);

/// One human's animation: its task stack and the controller that fills it.
class HumanAnimator {
  public:
    /// A controller over `anims` (which must outlive it) with `slots`, standing in its idle at once. Every slot the
    /// locomotion uses must have a clip (checked by CONEY_ASSERT; see clipsMissing()).
    HumanAnimator(const characters::AnimSet& anims, const AnimSlots& slots);

    /// How many of the clips the controller plays (slots 0, 4-7, 10 and 26, and the run start) `anims` lacks; 0 when
    /// it can run.
    [[nodiscard]] static std::size_t clipsMissing(const characters::AnimSet& anims, const AnimSlots& slots);

    /// Advances every task by `seconds` (the instance's animation step).
    void advance(float seconds);
    /// Picks the anim state and builds its tasks when it changes; in the move state also sets the gait blend's
    /// target from the speed, and swaps a walk start for a run start when a run is asked for early in it.
    /// @orig 0x00259578 Human_ChooseAnimState (unknown)
    void choose(const AnimInputs& inputs);

    /// The blended pose.
    [[nodiscard]] anim::Pose pose(std::span<const anim::Quat, anim::kPoseBones> bindRotations) const {
        return m_tasks.sample(bindRotations);
    }
    /// Whether a start clip is playing (human flag 0x10000000): the locomotion sets no velocity of its own then.
    [[nodiscard]] bool startClipPlaying() const;
    [[nodiscard]] AnimState state() const { return m_state; }
    /// The anim id playing (record `+0x20`): the newest task's.
    [[nodiscard]] std::uint32_t animId() const;
    /// The newest task's gait blend value, or -1 when it is not a gait blend.
    [[nodiscard]] float gaitValue() const;
    [[nodiscard]] const anim::AnimTaskStack& tasks() const { return m_tasks; }
    [[nodiscard]] const Speeds& speeds() const { return m_speeds; }

  private:
    // The idle: slot 0 looping with no task flags, after a fade of 0.15 s (shorter or longer over a start clip).
    // @orig 0x0025f770 Human_BuildIdleTasks (unknown)
    void buildIdle();
    // The move: a start clip into a gait blend from standing, or a new gait blend carrying on the old one's value and
    // phase when already moving.
    // @orig 0x0025b200 Human_BuildMoveTasks (unknown)
    void buildMove(bool run);
    // Coney's fall: the drop cycle (slot 26) looping after a 0.15 s fade.
    void buildFall();
    // A gait blend of slots 4, 5, 6, 7, 7 at `value` and normalised time `phase`, with the locomotion's flags.
    [[nodiscard]] std::unique_ptr<anim::GaitBlendTask> gaitBlend(float value, float phase) const;
    // The clip for slot `slot` with its anim id.
    [[nodiscard]] anim::GaitClip slotClip(std::size_t slot) const;

    const characters::AnimSet* m_anims;
    AnimSlots m_slots;
    Speeds m_speeds;
    anim::AnimTaskStack m_tasks;
    AnimState m_state = AnimState::None;
};

} // namespace coney::human
