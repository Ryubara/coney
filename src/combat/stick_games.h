// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "combat/stick.h"

// The minigames the combat input runs: the mugging (hold the stick on a target angle that keeps moving), the stereo
// theft's stick rotation (mode 3) and the L1/R1 button mash (mode 1). Each takes one update's input and game time and
// says whether it is still running, has succeeded or has failed. The stick here is the pad's own (x right, y up).
// Research: docs/research/combat.md#mugging, docs/research/combat.md#stereo-theft

namespace coney::combat {

/// Where a minigame stands after an update.
enum class GameResult : std::uint8_t { Running, Succeeded, Failed };

/// The mugging: the stick beyond CombatTuning::muggingStick within the tolerance of a target angle adds its time to
/// the progress; the target angle moves every period, by at least the tolerance plus 20°; the required time on target
/// succeeds, and the fail time since the start fails.
///
/// **Coney choices**: the first target angle is random; each move turns it by a random amount between the tolerance
/// plus 20° and 360° less that; the period counts game time from the start, not time on target; angles are the
/// stick's own, `atan2(x, y)` (the research leaves the frame open).
class MuggingGame {
  public:
    /// Starts at game time `startMs`, the first target drawn from `random`.
    MuggingGame(std::uint64_t startMs, CombatRandom& random);

    /// One update at game time `nowMs` with the stick at `stick`.
    /// @orig 0x002856b8 Player_UpdateMugging (unknown)
    GameResult update(std::uint64_t nowMs, Stick stick, const CombatTuning& tuning, CombatRandom& random);

    /// The angle the stick must hold, degrees, -180 to 180.
    [[nodiscard]] float targetDegrees() const { return m_target; }
    /// Time on target so far (record `+0x12c`), ms.
    [[nodiscard]] std::uint64_t progressMs() const { return m_progress; }
    /// The stick was on target in the last update (the struggle clips 342 / 343 play).
    [[nodiscard]] bool onTarget() const { return m_onTarget; }

  private:
    // Moves the target angle by at least the tolerance plus 20°.
    void moveTarget(const CombatTuning& tuning, CombatRandom& random);

    std::uint64_t m_startMs;
    std::uint64_t m_lastMs;
    std::uint64_t m_lastMoveMs;
    std::uint64_t m_progress = 0;
    float m_target = 0.0F;
    bool m_onTarget = false;
};

/// How many turns of the stick one stage of the stereo theft takes for a Warrior class byte `+0x0b`: 3 when it is 2
/// (as in the street), 1 otherwise (the exact condition is inferred).
[[nodiscard]] float stereoStageTurns(std::uint8_t warriorTheftByte);

/// The stereo theft, mode 3: rotate the left stick anticlockwise, beyond CombatTuning::theftStick in this update and
/// the last and less than 90° an update; the angle turned fills a stage, each stage advances after a 250 ms pause and
/// 4 stages succeed. Any command fails it.
///
/// **Coney choices**: a clockwise step neither adds nor takes away; the 250 ms pause ignores the stick.
class StereoTheft {
  public:
    /// Starts at game time `startMs`, each stage needing `stageTurns` turns (stereoStageTurns()).
    StereoTheft(std::uint64_t startMs, float stageTurns);

    /// One update at game time `nowMs` with this update's command and the stick at `stick`.
    /// @orig 0x0027e6d8 Player_UpdateTheft (unknown)
    GameResult update(std::uint64_t nowMs, CommandId command, Stick stick, const CombatTuning& tuning);

    /// Stages completed (`+0x4c`).
    [[nodiscard]] int stage() const { return m_stage; }
    /// Radians turned in the current stage (`+0x48`).
    [[nodiscard]] float turned() const { return m_turned; }

  private:
    float m_stageTarget;
    std::uint64_t m_pauseUntil = 0;
    bool m_pausing = false;
    float m_turned = 0.0F;
    int m_stage = 0;
    Stick m_last;
};

/// The button mash, mode 1: alternate L1 and R1. Each update the meter loses CombatTuning::mashDecay; each change from
/// one button to the other adds half the press gain times the Warrior class's factor (1.5 or 0.7, byte `+0x08`); the
/// target completes it.
///
/// **Coney choices**: the first press counts as an alternation; L1's and R1's pressed commands count as their held
/// ones; other commands are ignored (the research names failing commands only for mode 3); the gain is truncated to
/// a whole number.
class ButtonMash {
  public:
    /// One update with this update's command; `pressFactor` is the Warrior class's 1.5 or 0.7.
    GameResult update(CommandId command, float pressFactor, const CombatTuning& tuning);

    /// The meter, 0 up to the target.
    [[nodiscard]] int meter() const { return m_meter; }

  private:
    int m_meter = 0;
    int m_lastSide = 0; // 0 none, 1 L1, 2 R1
};

} // namespace coney::combat
