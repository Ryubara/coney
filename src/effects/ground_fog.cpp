// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/ground_fog.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::effects {

namespace {

// The game's frames a second.
constexpr float kFramesPerSecond = 60.0F;
// A wisp's drift is `drift` × 1.75-2.25 (docs/references/bindings/effects.md#start3dfog).
constexpr float kDriftMin = 1.75F;
constexpr float kDriftSpread = 0.5F;
// The sideways push of a wisp's aim, metres either way along the camera's x axis.
constexpr float kSideSpread = 2.0F;
// Below this a length is taken as none.
constexpr float kTiny = 1e-6F;
// The fade-in takes 9 / fadeSpeed steps.
constexpr float kFadeSteps = 9.0F;

// The squared horizontal and vertical distance between two points.
float distanceSquared(anim::Vec3 a, anim::Vec3 b) {
    const anim::Vec3 d = anim::subtract(a, b);
    return anim::dot(d, d);
}

} // namespace

float GroundFog::unit() {
    // xorshift32, from the fog's own seed.
    m_random ^= m_random << 13U;
    m_random ^= m_random >> 17U;
    m_random ^= m_random << 5U;
    constexpr std::uint32_t kSteps = 1U << 16U;
    return static_cast<float>(m_random % kSteps) / static_cast<float>(kSteps);
}

void GroundFog::start(const FogSettings& settings) {
    // The old emitter is killed and a new one made: its wisps go, and the new one keeps the default count.
    m_wisps.clear();
    m_settings = settings;
    m_maxWisps = kDefaultMaxWisps;
    m_frames = static_cast<float>(kTopUpFrames - 1); // the first top-up comes with the first frame
}

void GroundFog::stop() {
    m_settings.reset();
    m_wisps.clear();
}

void GroundFog::setMaxWisps(std::size_t count) {
    if (m_settings) {
        m_maxWisps = count;
    }
}

GroundFog::Wisp GroundFog::makeWisp(const EffectsViewer& viewer, float drift) {
    // Within kRadius of the target (uniform over the disc) and kMinHeight-kMaxHeight above it.
    const float angle = unit() * 2.0F * std::numbers::pi_v<float>;
    const float reach = kRadius * std::sqrt(unit());
    Wisp wisp;
    wisp.position = anim::Vec3{viewer.target.x + (reach * std::cos(angle)), viewer.target.y + (reach * std::sin(angle)),
                               viewer.target.z + kMinHeight + ((kMaxHeight - kMinHeight) * unit())};
    // Toward the camera, pushed up to 2 m to either side along the camera's own x axis (level, across the view).
    const anim::Vec3 forward = anim::subtract(viewer.target, viewer.position);
    anim::Vec3 side{forward.y, -forward.x, 0.0F};
    const float sideLength = std::sqrt(anim::dot(side, side));
    side = sideLength > kTiny ? anim::scale(side, 1.0F / sideLength) : anim::Vec3{1.0F, 0.0F, 0.0F};
    const float offset = kSideSpread * ((2.0F * unit()) - 1.0F);
    const anim::Vec3 aim = anim::add(anim::subtract(viewer.position, wisp.position), anim::scale(side, offset));
    const float aimLength = std::sqrt(anim::dot(aim, aim));
    const float speed = drift * (kDriftMin + (kDriftSpread * unit()));
    wisp.velocity = aimLength > kTiny ? anim::scale(aim, speed / aimLength) : anim::Vec3{};
    return wisp;
}

void GroundFog::step(float seconds, const EffectsViewer& viewer) {
    if (!m_settings) {
        return;
    }
    // Drop the wisps too far from the camera, then top the view up when a top-up is due.
    std::erase_if(m_wisps, [&viewer](const Wisp& wisp) {
        return distanceSquared(wisp.position, viewer.position) > kRadius * kRadius;
    });
    m_frames += seconds * kFramesPerSecond;
    if (m_frames >= static_cast<float>(kTopUpFrames)) {
        m_frames = std::fmod(m_frames - static_cast<float>(kTopUpFrames), static_cast<float>(kTopUpFrames));
        const std::size_t room = m_maxWisps > m_wisps.size() ? m_maxWisps - m_wisps.size() : 0;
        for (std::size_t i = 0; i < std::min(room, kWispsPerTopUp); ++i) {
            m_wisps.push_back(makeWisp(viewer, m_settings->drift));
        }
    }
    // Drift and fade in: each frame of the step is a fade step.
    const float target = m_settings->colour[3];
    const float steps = std::max(1.0F, std::floor(kFadeSteps / std::max(m_settings->fadeSpeed, 1e-6F)));
    const float perFrame = target / steps * std::max(1.0F, std::trunc(m_settings->fadeRate));
    for (Wisp& wisp : m_wisps) {
        wisp.position = anim::add(wisp.position, anim::scale(wisp.velocity, seconds));
        wisp.alpha = std::min(target, wisp.alpha + (perFrame * seconds * kFramesPerSecond));
        wisp.hidden = distanceSquared(wisp.position, viewer.position) < kHideWithin * kHideWithin;
    }
}

} // namespace coney::effects
