// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/room_smoke.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::effects {

namespace {

// A drift's scroll per update before the amount: 0.0005-0.0015 of the texture's width.
constexpr float kMinScroll = 0.0005F;
constexpr float kScrollRange = 0.001F;
// A drift's width and height scales.
constexpr float kMinWidthScale = 1.63F;
constexpr float kWidthScaleRange = 0.3F;
constexpr float kMinHeightScale = 1.0F;
constexpr float kHeightScaleRange = 0.2F;
// The sprite's width before the scale, overlay units, and its height's factor (`0x0050cf00`, 1.0).
constexpr float kBaseWidth = 1.3F;
constexpr float kBaseHeight = 1.0F;
// The tilt the height follows, degrees, and the GUI heights it maps to.
constexpr float kLowestPitch = -50.0F;
constexpr float kHighestPitch = 10.0F;
constexpr float kLowestY = -0.25F;
constexpr float kHighestY = 0.26F;
// The heading's range once mapped, and the factor on its change that slides the haze.
constexpr float kHeadingTop = 0.9999F;
constexpr float kTurnScroll = 2.0F;
// A fixed step's tick may land a hair early through float rounding; this much early still ticks, ms.
constexpr float kTickSlackMs = 0.01F;

// `value` from [from0, from1] onto [to0, to1], clamped (`Math_MapRange`).
float mapRange(float value, float from0, float from1, float to0, float to1) {
    const float share = std::clamp((value - from0) / (from1 - from0), 0.0F, 1.0F);
    return to0 + ((to1 - to0) * share);
}

// The part of `a` toward `b` at `t`.
float lerp(float a, float b, float t) { return (a * (1.0F - t)) + (b * t); }

} // namespace

float RoomSmoke::unit() {
    // xorshift32.
    m_random ^= m_random << 13U;
    m_random ^= m_random >> 17U;
    m_random ^= m_random << 5U;
    return static_cast<float>(m_random >> 8U) / static_cast<float>(1U << 24U);
}

SmokeDrift RoomSmoke::pickDrift() {
    SmokeDrift drift;
    const float scroll = (kMinScroll + (unit() * kScrollRange)) * m_settings.amount;
    drift.scroll = unit() < 0.5F ? -scroll : scroll;
    drift.widthScale = kMinWidthScale + (unit() * kWidthScaleRange);
    drift.heightScale = kMinHeightScale + (unit() * kHeightScaleRange);
    const int low = std::min(m_settings.lowestAlpha, m_settings.highestAlpha);
    const int high = std::max(m_settings.lowestAlpha, m_settings.highestAlpha);
    drift.alpha = static_cast<float>(low + std::min(high - low, static_cast<int>(unit() * float(high - low + 1))));
    const std::uint32_t span = kMaxBlendMs - kMinBlendMs;
    m_blendMs = static_cast<float>(kMinBlendMs + std::min(span, static_cast<std::uint32_t>(unit() * float(span + 1))));
    return drift;
}

void RoomSmoke::start(const RoomSmokeSettings& settings) {
    m_settings = settings;
    if (m_on) {
        // A new record for the running smoke: the next tick jumps to a new target.
        m_to = pickDrift();
        m_blendMs = 1.0F;
        return;
    }
    m_on = true;
    m_sprite.u = unit();
    m_from = pickDrift();
    m_now = m_from;
    m_to = pickDrift();
    m_blendStartMs = m_clockMs;
    m_heading.reset();
}

void RoomSmoke::blend() {
    const float t = std::min((m_clockMs - m_blendStartMs) / std::max(m_blendMs, 1.0F), 1.0F);
    m_now.scroll = lerp(m_from.scroll, m_to.scroll, t);
    m_now.widthScale = lerp(m_from.widthScale, m_to.widthScale, t);
    m_now.heightScale = lerp(m_from.heightScale, m_to.heightScale, t);
    m_now.alpha = std::round(lerp(m_from.alpha, m_to.alpha, t));
    if (t >= 1.0F) {
        m_from = m_to;
        m_blendStartMs = m_clockMs;
        m_to = pickDrift();
    }
}

float RoomSmoke::followCamera(const EffectsViewer& viewer) {
    const anim::Vec3 d{viewer.target.x - viewer.position.x, viewer.target.y - viewer.position.y,
                       viewer.target.z - viewer.position.z};
    const float length = std::sqrt((d.x * d.x) + (d.y * d.y) + (d.z * d.z));
    if (length < 1e-6F) {
        return 0.0F;
    }
    // The tilt: the angle from straight down, less 90°, so 0 is level and looking down is negative.
    const float down = std::clamp(-d.z / length, -1.0F, 1.0F);
    const float pitch = (std::acos(down) * 180.0F / std::numbers::pi_v<float>)-90.0F;
    m_sprite.guiY = mapRange(pitch, kLowestPitch, kHighestPitch, kLowestY, kHighestY);
    // The turn: the heading's change since the last tick slides the haze.
    const float degrees = std::atan2(d.y, d.x) * 180.0F / std::numbers::pi_v<float>;
    const float heading = mapRange(degrees, -180.0F, 180.0F, 0.0F, kHeadingTop);
    const float slide = m_heading ? kTurnScroll * (*m_heading - heading) : 0.0F;
    m_heading = heading;
    return slide;
}

void RoomSmoke::step(float seconds, const std::optional<EffectsViewer>& viewer) {
    m_clockMs += seconds * 1000.0F;
    if (!m_on || !viewer || m_clockMs - m_lastTickMs < (1000.0F / kTicksPerSecond) - kTickSlackMs) {
        return;
    }
    m_lastTickMs = m_clockMs;
    blend();
    const float slide = followCamera(*viewer);
    m_sprite.width = kBaseWidth * m_now.widthScale;
    m_sprite.height = kBaseHeight * m_now.heightScale;
    m_sprite.colour = {m_settings.tint[0], m_settings.tint[1], m_settings.tint[2],
                       static_cast<std::uint8_t>(std::clamp(m_now.alpha, 0.0F, 255.0F))};
    const float u = m_sprite.u + m_now.scroll + slide;
    m_sprite.u = u - std::floor(u);
}

} // namespace coney::effects
