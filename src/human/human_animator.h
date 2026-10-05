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
// changes, builds the animation tasks for it; while moving it keeps the gait blend's target on the speed. The human
// starts the actions (a jump, its landing, a run stop, a climb) itself; their clips play out before the controller
// chooses again.
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
inline constexpr std::size_t kSlotJumpLoop = 30;
inline constexpr std::size_t kSlotRunStop = 33;

/// The jump's landing clips (not slots): 435 standing (jump end), 436 moving on (jump end running).
inline constexpr std::uint32_t kAnimJumpEnd = 435;
inline constexpr std::uint32_t kAnimJumpEndRunning = 436;
/// The climb clips: 437 to 460, four climbs of a standing and a running chain of three (src/human/climb.h).
inline constexpr std::uint32_t kAnimFirstClimb = 437;
inline constexpr std::uint32_t kAnimLastClimb = 460;

/// Anim states, by the original's numbers where it has them.
enum class AnimState : std::int8_t {
    None = -1, ///< Nothing built yet.
    Idle = 0,
    Move = 4,
    Jump = 25,     ///< The jump loop (slot 30) until the landing.
    Fall = 26,     ///< A drop: **Coney's builder** loops the drop cycle (slot 26).
    Land = 27,     ///< A jump's landing clip, then the gait blend or the idle.
    RunStop = 101, ///< **Coney's**: the run stop (slot 33) after a sprint's skid, then the idle.
    Climb = 102,   ///< **Coney's**: a climb's three clips, then the gait blend or the idle.
};

/// What the controller decides from, each update.
struct AnimInputs {
    float speed = 0.0F;     ///< The human's horizontal speed, m/s.
    bool wantsMove = false; ///< The stick asks for movement (above the dead zone).
    bool wantsRun = false;  ///< The stick asks for a run (above kRunThreshold).
    bool airborne = false;  ///< The human is falling or jumping.
};

/// The fade lengths of the builders, seconds.
inline constexpr float kIdleFade = 0.15F;
inline constexpr float kIdleFadeEarlyStart = 1.0F / 15.0F;
inline constexpr float kIdleFadeLateStart = 0.2F;
inline constexpr float kStartClipEarly = 0.1333F;
inline constexpr float kMoveFadeMoving = 0.1333F;
/// The jump loop's fade (the state 25 builder's 0.1 s); **Coney's** for the landing too.
inline constexpr float kJumpFade = 0.1F;
/// The gait blend's value speed, units a second, for the locomotion.
inline constexpr float kGaitValueSpeed = 10.0F;

/// The human's speeds from its anim set and slots: each slot's clip's root motion.
/// @orig 0x00254078 Human_ComputeSpeeds (unknown)
[[nodiscard]] Speeds speedsOf(const characters::AnimSet& anims, const AnimSlots& slots);

/// One human's animation: its task stack and the controller that fills it.
class HumanAnimator {
  public:
    /// A controller over `anims` (which must outlive it) with `slots`, standing in its idle at once. Every clip the
    /// controller plays must exist (checked by CONEY_ASSERT; see clipsMissing()).
    HumanAnimator(const characters::AnimSet& anims, const AnimSlots& slots);

    /// How many of the clips the controller plays (slots 0, 4-7, 10, 26, 30 and 33, the run start, the two landings
    /// and the 24 climb clips) `anims` lacks; 0 when it can run.
    [[nodiscard]] static std::size_t clipsMissing(const characters::AnimSet& anims, const AnimSlots& slots);

    /// Advances every task by `seconds` (the instance's animation step).
    void advance(float seconds);
    /// Picks the anim state and builds its tasks when it changes; in the move state also sets the gait blend's
    /// target from the speed, and swaps a walk start for a run start when a run is asked for early in it. While an
    /// action's clips play (a landing, a run stop, a climb) nothing changes; the jump loop plays until the landing.
    /// @orig 0x00259578 Human_ChooseAnimState (unknown)
    void choose(const AnimInputs& inputs);

    /// The jump: the jump loop (slot 30) after a 0.1 s fade.
    /// @orig 0x0025cf30 Human_BuildJumpTasks (unknown)
    void startJump();
    /// A jump's landing: moving on (the stick above the dead zone), the jump end running (436) handing over to a gait
    /// blend at the jog; otherwise the jump end (435) and then the idle. **Coney's choice**: the jump's 0.1 s fade.
    /// @orig 0x0025d390 Human_BuildLandTasks (unknown)
    void startLanding(bool movingOn);
    /// The run stop after a sprint's skid (slot 33, 417), then the idle. **Coney's**: the page sees it after a sprint
    /// and not after a run, and does not trace what plays it; the move fade of 0.1333 s is Coney's too.
    void startRunStop();
    /// A climb: the chain `firstId`, + 1, + 2, then a gait blend at the run (`running`) or the idle. **Coney's
    /// choice**: the move fade of 0.1333 s before it.
    void startClimb(std::uint32_t firstId, bool running);
    /// Ends whatever plays with the idle (a climb that cannot go on).
    void stopToIdle();

    /// The blended pose.
    [[nodiscard]] anim::Pose pose(std::span<const anim::Quat, anim::kPoseBones> bindRotations) const {
        return m_tasks.sample(bindRotations);
    }
    /// Whether a move's start clip is playing (human flag 0x10000000): no jump then.
    [[nodiscard]] bool startClipPlaying() const;
    /// Whether a clip that moves the body by its root is playing (a start clip, a landing, a run stop, a climb's): the
    /// locomotion sets no velocity of its own then (flags `0x110c0880`,
    /// docs/research/formats/animation.md#root-motion).
    [[nodiscard]] bool drivingClipPlaying() const;
    /// Whether an action's clips are playing (a landing, a run stop or a climb: record `+0x08` is not 0).
    [[nodiscard]] bool actionPlaying() const;
    [[nodiscard]] AnimState state() const { return m_state; }
    /// The anim id playing (record `+0x20`): the newest task's.
    [[nodiscard]] std::uint32_t animId() const;
    /// The newest task's gait blend value, or -1 when it is not a gait blend.
    [[nodiscard]] float gaitValue() const;
    [[nodiscard]] const anim::AnimTaskStack& tasks() const { return m_tasks; }
    [[nodiscard]] const Speeds& speeds() const { return m_speeds; }
    [[nodiscard]] const characters::AnimSet& anims() const { return *m_anims; }

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
    // The idle loop task (slot 0).
    [[nodiscard]] std::unique_ptr<anim::AnimTask> idleLoop() const;
    // A clip `id` played once (at its range-flag rate, no task flags) that hands over to `next`.
    [[nodiscard]] std::unique_ptr<anim::AnimTask> clipThen(std::uint32_t id,
                                                           std::unique_ptr<anim::AnimTask> next) const;
    // The clip for slot `slot` with its anim id.
    [[nodiscard]] anim::GaitClip slotClip(std::size_t slot) const;

    const characters::AnimSet* m_anims;
    AnimSlots m_slots;
    Speeds m_speeds;
    anim::AnimTaskStack m_tasks;
    AnimState m_state = AnimState::None;
};

} // namespace coney::human
