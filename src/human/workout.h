// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The rules of one workout update on gym equipment (state code 0x1b, the clubhouse's chin-ups, push-ups and sit-ups):
// the effort a pad's cross presses pump up and that decays otherwise, the loop blend's target and rate it gives, and
// when the workout ends. Pure: the caller reads the blend's value and clip time and applies the result to the blend.
// Research: docs/research/workouts.md#engine

namespace coney::human {

/// `CfgWorkoutParams`' tables (`0x00510690` gain, `0x005106a0` decay, `0x005106b0` hold, one entry per level of the
/// blend) and its fourth argument read as a flag (`0x005106ac`, 0 or 1).
struct WorkoutParams {
    std::array<float, 3> gain{};
    std::array<float, 3> decay{};
    std::array<int, 3> hold{};
    float factor = 0.0F;
};

/// What one update asks of the loop blend: its target value (0-2: slow, mid, fast) and its rate, and whether the
/// workout ends now (the action becomes `0x1c`).
struct WorkoutStep {
    float target = 0.0F;
    float rate = 1.0F;
    bool end = false;
};

/// One human's effort (`+0x670`) and hold counter (`+0x674`) through a workout.
/// @orig 0x00255540 Human_UpdateWorkout (unknown)
class WorkoutEffort {
  public:
    /// The effort and the hold counter the workout's start sets.
    static constexpr float kStartEffort = 1.0F;
    static constexpr int kStartHold = 6;

    /// The level of the blend that indexes the tables: floor(`value` + 0.5), 0-2.
    [[nodiscard]] static std::size_t levelOf(float value);

    /// One update of a pad's human, with the blend at `value` and its lower clip at `lowerTime` of `lowerDuration`
    /// seconds: `pumped` when the command is cross newly pressed (`0x12`), `quit` when it is triangle newly pressed
    /// (10). A pump sets the hold counter to the level's hold; while the counter runs (counted down on updates without
    /// a pump) the level's gain over the timing factor is added, up to 1; an update that adds nothing takes the
    /// level's decay (× 0.3 below an effort of 1/3, else × the factor), never below 0. Below an effort of 1/3 the
    /// target is 0, and once the blend is at 0 the rate is 3 × effort + 2 × decay; otherwise the target is
    /// 3 × effort − 1 at rate 1. It ends on triangle, or at a blend of 0 once the rate the update before set is at
    /// most 3 × decay.
    [[nodiscard]] WorkoutStep padUpdate(const WorkoutParams& params, float value, float lowerTime, float lowerDuration,
                                        bool pumped, bool quit);
    /// One update of a human no pad drives: the effort is what HuSetWorkoutBlend wrote (no pumps, no decay, no end of
    /// its own); below 1 the target is 0 and, at a blend of 0, the rate is effort + 3 × the first decay; from 1 the
    /// target is effort − 1.
    [[nodiscard]] WorkoutStep aiUpdate(const WorkoutParams& params, float value) const;

    /// The timing factor: 1, unless the parameters' factor is not 1 and the lower clip is in its first half, when it
    /// is factor × (1 − time / (duration / 2)). The scripts always make it 1.
    [[nodiscard]] static float timingFactor(const WorkoutParams& params, float lowerTime, float lowerDuration);

    /// The effort now, and HuSetWorkoutBlend's write of it.
    [[nodiscard]] float effort() const { return m_effort; }
    void setEffort(float effort) { m_effort = effort; }
    /// The hold counter now.
    [[nodiscard]] int hold() const { return m_hold; }

  private:
    float m_effort = kStartEffort;
    int m_hold = kStartHold;
    float m_lastRate = 1.0F; // the rate the update before set
};

} // namespace coney::human
