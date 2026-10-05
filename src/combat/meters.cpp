// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/meters.h"

#include <algorithm>
#include <cmath>

#include "core/assert.h"

namespace coney::combat {

namespace {

// Advances a rate-per-second meter over `elapsedMs`: the whole points to move now, with the fraction left in `carry`.
int wholePoints(float ratePerSecond, std::uint64_t elapsedMs, float& carry) {
    const float amount = (ratePerSecond * static_cast<float>(elapsedMs) / 1000.0F) + carry;
    const float whole = std::floor(amount);
    carry = amount - whole;
    return static_cast<int>(whole);
}

} // namespace

Health::Health(int maximum) : Health(maximum, maximum) {}

Health::Health(int value, int maximum) : m_value(std::clamp(value, 0, maximum)), m_maximum(maximum) {
    CONEY_ASSERT(maximum > 0);
}

float Health::fraction() const { return static_cast<float>(m_value) / static_cast<float>(m_maximum); }

int Health::apply(int damage) {
    const int taken = std::clamp(damage, 0, m_value);
    m_value -= taken;
    return taken;
}

void PendingDamage::add(int damage, int kind) {
    if (damage > m_damage) {
        m_damage = damage;
        m_kind = kind;
    }
}

int PendingDamage::applyTo(Health& health) {
    const int taken = health.apply(m_damage);
    m_damage = 0;
    m_kind = 0;
    return taken;
}

int strikeDamage(const AnimRangeList& ranges, int animId, bool doubled, bool quartered) {
    if (animId < 0) {
        return 0;
    }
    int damage = ranges.damage(static_cast<std::size_t>(animId));
    if (doubled) {
        damage *= 2;
    }
    if (quartered) {
        damage /= 4;
    }
    return damage;
}

PowerMeter::PowerMeter(int maximum, int refillPerSecond, std::uint64_t startMs)
    : m_value(maximum), m_maximum(maximum), m_refillPerSecond(refillPerSecond), m_lastMs(startMs) {
    CONEY_ASSERT(maximum > 0);
}

float PowerMeter::fraction() const { return static_cast<float>(m_value) / static_cast<float>(m_maximum); }

int PowerMeter::spend(float fraction) {
    const auto cost = static_cast<int>(std::lround(fraction * static_cast<float>(m_maximum)));
    const int spent = std::clamp(cost, 0, m_value);
    m_value -= spent;
    return spent;
}

void PowerMeter::set(int value) { m_value = std::clamp(value, 0, m_maximum); }

void PowerMeter::update(std::uint64_t nowMs, bool draining, float drainPerSecond) {
    const std::uint64_t elapsed = nowMs > m_lastMs ? nowMs - m_lastMs : 0;
    m_lastMs = nowMs;
    if (draining != m_draining) {
        m_carry = 0.0F;
        m_draining = draining;
    }
    if (draining) {
        m_value = std::max(0, m_value - wholePoints(drainPerSecond, elapsed, m_carry));
        return;
    }
    if (m_value >= m_maximum) {
        // A full meter carries nothing into the next drain.
        m_carry = 0.0F;
        return;
    }
    m_value = std::min(m_maximum, m_value + wholePoints(static_cast<float>(m_refillPerSecond), elapsed, m_carry));
}

RageMeter::RageMeter(int maximum, int gainPercent, std::uint64_t startMs)
    : m_maximum(maximum), m_gainPercent(gainPercent), m_lastMs(startMs) {
    CONEY_ASSERT(maximum > 0);
}

void RageMeter::set(int value) { m_value = std::clamp(value, 0, m_maximum); }

int RageMeter::add(float points, const CombatTuning& tuning, RageGain gain) {
    if (m_raging || points <= 0.0F) {
        return 0;
    }
    // The points up to the cap count in full, the rest at the factor above it.
    const float below = std::min(points, tuning.ragePointsCap);
    const float above = std::max(points - tuning.ragePointsCap, 0.0F);
    const float scaled = (below * tuning.rageFactorBelow) + (above * tuning.rageFactorAbove);
    const float halving = gain.halved ? 0.5F : 1.0F;
    const float raw = scaled * static_cast<float>(m_gainPercent) / 100.0F * halving * gain.stateMultiplier;
    const auto added = std::clamp(static_cast<int>(std::lround(raw)), 0, m_maximum - m_value);
    m_value += added;
    return added;
}

bool RageMeter::start(std::uint64_t nowMs) {
    if (m_raging || !full()) {
        return false;
    }
    m_raging = true;
    m_lastMs = nowMs;
    m_carry = 0.0F;
    return true;
}

void RageMeter::update(std::uint64_t nowMs, float drainPerSecond) {
    const std::uint64_t elapsed = nowMs > m_lastMs ? nowMs - m_lastMs : 0;
    m_lastMs = nowMs;
    if (!m_raging) {
        return;
    }
    m_value = std::max(0, m_value - wholePoints(drainPerSecond, elapsed, m_carry));
    if (m_value == 0) {
        m_raging = false;
        m_carry = 0.0F;
    }
}

} // namespace coney::combat
