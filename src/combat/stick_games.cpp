// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/stick_games.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::combat {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// A new mugging target angle lies more than the re-roll gap plus this from the last, degrees.
constexpr float kMugMoveMargin = 20.0F;
// The most tries a new mugging target angle gets to clear the gap.
constexpr int kMugRerolls = 64;

// `degrees` wrapped into (-180, 180].
float wrapDegrees(float degrees) {
    float wrapped = std::fmod(degrees, 360.0F);
    if (wrapped > 180.0F) {
        wrapped -= 360.0F;
    } else if (wrapped <= -180.0F) {
        wrapped += 360.0F;
    }
    return wrapped;
}

// `radians` wrapped into (-pi, pi].
float wrapRadians(float radians) {
    float wrapped = std::fmod(radians, 2.0F * kPi);
    if (wrapped > kPi) {
        wrapped -= 2.0F * kPi;
    } else if (wrapped <= -kPi) {
        wrapped += 2.0F * kPi;
    }
    return wrapped;
}

// The stick's anticlockwise angle from the right, radians: what the theft's rotation is measured in.
float anticlockwiseAngle(Stick stick) { return std::atan2(stick.y, stick.x); }

// Which mash button a command is: 1 for L1, 2 for R1, 0 for neither.
int mashSide(CommandId command) {
    if (command == command::kL1Held || command == command::kL1Pressed) {
        return 1;
    }
    if (command == command::kR1Held || command == command::kR1Pressed) {
        return 2;
    }
    return 0;
}

} // namespace

MuggingParams muggingParams(const CombatTuning& tuning) {
    return MuggingParams{.requiredMs = tuning.muggingRequiredMs,
                         .periodMs = tuning.muggingPeriodMs,
                         .offTargetMs = tuning.muggingFailMs,
                         .toleranceDegrees = tuning.muggingToleranceDegrees,
                         .gapDegrees = tuning.muggingGapDegrees};
}

MuggingGame::MuggingGame(std::uint64_t startMs, CombatRandom& random, const MuggingParams& params)
    : m_params(params), m_lastMs(startMs) {
    m_target = wrapDegrees((random.unit() * 360.0F) - 180.0F);
}

GameResult MuggingGame::update(std::uint64_t nowMs, Stick stick, const CombatTuning& tuning, CombatRandom& random) {
    const std::uint64_t elapsed = nowMs > m_lastMs ? nowMs - m_lastMs : 0;
    m_lastMs = nowMs;

    // On target: the stick out past the gate and within the tolerance of the angle.
    const float off = std::fabs(wrapDegrees(stick.angleDegrees() - m_target));
    m_onTarget = stick.magnitude() > tuning.muggingStick && off <= m_params.toleranceDegrees;
    if (!m_onTarget) {
        // The time off target adds up over the whole mugging; past the allowance it fails.
        m_offTarget += elapsed;
        return m_offTarget > static_cast<std::uint64_t>(std::max(m_params.offTargetMs, 0)) ? GameResult::Failed
                                                                                           : GameResult::Running;
    }
    const std::uint64_t before = m_progress;
    m_progress += elapsed;
    if (m_progress >= static_cast<std::uint64_t>(std::max(m_params.requiredMs, 0))) {
        return GameResult::Succeeded;
    }
    // Each multiple of the period the progress passes brings a new target angle.
    if (m_params.periodMs > 0) {
        const auto period = static_cast<std::uint64_t>(m_params.periodMs);
        if (m_progress / period > before / period) {
            moveTarget(random);
        }
    }
    return GameResult::Running;
}

void MuggingGame::moveTarget(CombatRandom& random) {
    const float least = m_params.gapDegrees + kMugMoveMargin;
    float next = m_target;
    for (int attempt = 0; attempt < kMugRerolls; ++attempt) {
        next = wrapDegrees((random.unit() * 360.0F) - 180.0F);
        if (std::fabs(wrapDegrees(next - m_target)) > least) {
            break;
        }
    }
    m_target = next;
}

float stereoStageTurns(std::uint8_t warriorTheftByte) { return warriorTheftByte == 2 ? 3.0F : 1.0F; }

StereoTheft::StereoTheft(std::uint64_t startMs, float stageTurns)
    : m_stageTarget(stageTurns * 2.0F * kPi), m_pauseUntil(startMs) {}

GameResult StereoTheft::update(std::uint64_t nowMs, CommandId command, Stick stick, const CombatTuning& tuning) {
    // Any command fails the game.
    if (command != command::kNone) {
        return GameResult::Failed;
    }
    const Stick last = m_last;
    m_last = stick;

    // After a stage's target, the stage advances once the pause is over.
    if (m_pausing) {
        if (nowMs < m_pauseUntil) {
            return GameResult::Running;
        }
        m_pausing = false;
        ++m_stage;
        m_turned = 0.0F;
        if (m_stage >= tuning.theftStages) {
            return GameResult::Succeeded;
        }
    }

    // An anticlockwise step of less than the limit, with the stick out in both samples, adds to the stage.
    if (stick.magnitude() > tuning.theftStick && last.magnitude() > tuning.theftStick) {
        const float step = wrapRadians(anticlockwiseAngle(stick) - anticlockwiseAngle(last));
        if (step > 0.0F && step < tuning.theftMaxStepDegrees * kPi / 180.0F) {
            m_turned += step;
        }
    }
    if (m_turned >= m_stageTarget) {
        m_pausing = true;
        m_pauseUntil = nowMs + static_cast<std::uint64_t>(std::max(tuning.theftStagePauseMs, 0));
    }
    return GameResult::Running;
}

GameResult ButtonMash::update(CommandId command, float pressFactor, const CombatTuning& tuning) {
    m_meter = std::max(0, m_meter - tuning.mashDecay);
    const int side = mashSide(command);
    if (side != 0 && side != m_lastSide) {
        m_lastSide = side;
        m_meter += static_cast<int>(static_cast<float>(tuning.mashPressGain) / 2.0F * pressFactor);
    }
    if (m_meter >= tuning.mashTarget) {
        m_meter = tuning.mashTarget;
        return GameResult::Succeeded;
    }
    return GameResult::Running;
}

} // namespace coney::combat
