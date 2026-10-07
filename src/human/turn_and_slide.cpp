// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/turn_and_slide.h"

#include <algorithm>
#include <cmath>

#include "human/locomotion.h"

namespace coney::human {

void TurnAndSlide::turnToOver(float heading, float target, float seconds) {
    m_turnRate = 0.0F;
    m_turnTime = 0.0F;
    const float angle = wrapAngle(target - heading);
    if (seconds <= 0.0F || std::fabs(angle) < kSteerMinTurn) {
        return;
    }
    m_turnRate = angle / seconds;
    m_turnTime = seconds;
}

void TurnAndSlide::moveToOver(anim::Vec3 from, anim::Vec3 goal, float seconds) {
    m_slide = anim::Vec3{};
    m_slideTime = 0.0F;
    const anim::Vec3 move{goal.x - from.x, goal.y - from.y, 0.0F};
    const float distance = std::hypot(move.x, move.y);
    if (seconds <= 0.0F || distance < kSteerMinSlide || distance >= kSteerMaxSlide) {
        return;
    }
    // Too fast over more than one step is refused; within one step any speed goes.
    if (distance / seconds > kSteerMaxSlideSpeed && seconds > kStepSeconds) {
        return;
    }
    m_slide = anim::scale(move, 1.0F / seconds);
    m_slideTime = seconds;
}

TurnAndSlideStep TurnAndSlide::step(float dt) {
    TurnAndSlideStep out;
    if (dt <= 0.0F) {
        return out;
    }
    // The turn: rate × the step, the last step only for the time left.
    if (m_turnTime > 0.0F) {
        out.turn = m_turnRate * std::min(dt, m_turnTime);
        m_turnTime = std::max(0.0F, m_turnTime - dt);
    }
    // The slide: its velocity, scaled on the last step by the share of it left.
    if (m_slideTime > 0.0F) {
        out.velocity = anim::scale(m_slide, std::min(dt, m_slideTime) / dt);
        m_slideTime = std::max(0.0F, m_slideTime - dt);
    }
    return out;
}

void TurnAndSlide::clear() {
    m_turnRate = 0.0F;
    m_turnTime = 0.0F;
    m_slide = anim::Vec3{};
    m_slideTime = 0.0F;
}

bool isContactEvent(unsigned type) {
    return type == 0x9 || type == 0xf || type == 0x13 || type == 0x2c || type == 0x34 || type == 0x36 || type == 0x41;
}

float firstContactTime(const anim::AnimClip& clip, float rate) {
    const float playRate = rate > 0.0F ? rate : 1.0F;
    // The first event that ends the steer, by frame; with none, the clip's whole playing time.
    float seconds = clip.duration / playRate;
    bool found = false;
    for (const anim::ClipEvent& event : clip.events) {
        if (!isContactEvent(event.type)) {
            continue;
        }
        const float at = static_cast<float>(event.frame) / anim::kClipFrameRate / playRate;
        if (!found || at < seconds) {
            seconds = at;
            found = true;
        }
    }
    return seconds;
}

SteerGoal attackSteerGoal(anim::Vec3 from, anim::Vec3 target, anim::Vec3 targetVelocity, float reach, float seconds) {
    // The target where it will be: led by its velocity, a long lead that carries it away from the attacker cut to
    // kSteerLeadCut (docs/research/combat-moves.md#reach).
    anim::Vec3 lead{targetVelocity.x * (seconds + kSteerLeadExtraSeconds),
                    targetVelocity.y * (seconds + kSteerLeadExtraSeconds), 0.0F};
    const anim::Vec3 away{target.x - from.x, target.y - from.y, 0.0F};
    if (const float leadLength = std::hypot(lead.x, lead.y);
        leadLength > kSteerLeadHalveBeyond && anim::dot(lead, away) > 0.0F) {
        lead = anim::scale(lead, kSteerLeadCut / leadLength);
    }
    const anim::Vec3 ahead{target.x + lead.x, target.y + lead.y, from.z};
    // Back from there by the reach, along the line from the attacker.
    const anim::Vec3 line{ahead.x - from.x, ahead.y - from.y, 0.0F};
    const float length = std::hypot(line.x, line.y);
    if (length < 1e-6F) {
        return SteerGoal{.aim = ahead, .stand = from};
    }
    return SteerGoal{.aim = ahead,
                     .stand =
                         anim::Vec3{ahead.x - (line.x / length * reach), ahead.y - (line.y / length * reach), from.z}};
}

} // namespace coney::human
