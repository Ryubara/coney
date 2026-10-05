// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/stick_games.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::combat {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// The mugging target moves by at least its tolerance plus this, degrees.
constexpr float kMugMoveMargin = 20.0F;

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

MuggingGame::MuggingGame(std::uint64_t startMs, CombatRandom& random)
    : m_startMs(startMs), m_lastMs(startMs), m_lastMoveMs(startMs) {
    m_target = wrapDegrees((random.unit() * 360.0F) - 180.0F);
}

GameResult MuggingGame::update(std::uint64_t nowMs, Stick stick, const CombatTuning& tuning, CombatRandom& random) {
    const std::uint64_t elapsed = nowMs > m_lastMs ? nowMs - m_lastMs : 0;
    m_lastMs = nowMs;

    // Time with the stick on target counts towards the mugging.
    const float off = std::fabs(wrapDegrees(stick.angleDegrees() - m_target));
    m_onTarget = stick.magnitude() > tuning.muggingStick && off <= tuning.muggingToleranceDegrees;
    if (m_onTarget) {
        m_progress += elapsed;
    }
    if (m_progress >= static_cast<std::uint64_t>(std::max(tuning.muggingRequiredMs, 0))) {
        return GameResult::Succeeded;
    }
    if (nowMs - m_startMs >= static_cast<std::uint64_t>(std::max(tuning.muggingFailMs, 0))) {
        return GameResult::Failed;
    }

    // Every period the target moves on.
    if (tuning.muggingPeriodMs > 0 && nowMs - m_lastMoveMs >= static_cast<std::uint64_t>(tuning.muggingPeriodMs)) {
        moveTarget(tuning, random);
        m_lastMoveMs = nowMs;
    }
    return GameResult::Running;
}

void MuggingGame::moveTarget(const CombatTuning& tuning, CombatRandom& random) {
    const float least = std::min(tuning.muggingToleranceDegrees + kMugMoveMargin, 180.0F);
    const float turn = least + (random.unit() * (360.0F - (2.0F * least)));
    m_target = wrapDegrees(m_target + turn);
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
