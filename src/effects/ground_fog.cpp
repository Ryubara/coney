// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/ground_fog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "core/assert.h"

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
// The fade-in takes 9 / fadeSpeed updates; with a 0 alpha step, 0.6 / fadeSpeed.
constexpr float kFadeSteps = 9.0F;
constexpr float kSlowFadeSteps = 0.6F;
// A wisp's updates before one whose previous alpha was 0 ends: it ends from its third.
constexpr std::uint32_t kEndFromUpdate = 3;

// The squared horizontal and vertical distance between two points.
float distanceSquared(anim::Vec3 a, anim::Vec3 b) {
    const anim::Vec3 d = anim::subtract(a, b);
    return anim::dot(d, d);
}

// `colour`'s alpha replaced by `alpha`, packed `0xRRGGBBAA`.
std::uint32_t packed(const std::array<std::uint8_t, 4>& colour, std::uint8_t alpha) {
    return (static_cast<std::uint32_t>(colour[0]) << 24U) | (static_cast<std::uint32_t>(colour[1]) << 16U) |
           (static_cast<std::uint32_t>(colour[2]) << 8U) | alpha;
}

} // namespace

bool nearView(anim::Vec3 eye, const ViewWindow& window, anim::Vec3 point, float margin) {
    // The signed distance to each plane, inward positive: near and far along the view, then the four sides through
    // the eye, each normal the inward one of its edge of the view window.
    const anim::Vec3 d = anim::subtract(point, eye);
    const float ahead = anim::dot(d, window.forward);
    if (ahead - window.nearClip < -margin || window.farClip - ahead < -margin) {
        return false;
    }
    const auto side = [&d, ahead](anim::Vec3 axis, float tangent) {
        // Planes at ±axis: inward normals (tangent × forward ∓ axis), normalised.
        const float across = anim::dot(d, axis);
        const float length = std::sqrt((tangent * tangent) + 1.0F);
        return std::min(((tangent * ahead) - across) / length, ((tangent * ahead) + across) / length);
    };
    return side(window.right, window.tanHalfWidth) >= -margin && side(window.up, window.tanHalfHeight) >= -margin;
}

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
    m_ticks = 0.0F;
    // The fade: ⌊9 / fadeSpeed⌋ updates (at least 1) of ⌊alpha / updates⌋ × ⌊fadeRate⌋ each, kept to a byte; a step
    // that comes out 0 is 1, every 30 ticks, over ⌊0.6 / fadeSpeed⌋ updates.
    const float speed = std::max(settings.fadeSpeed, 1e-6F);
    m_fadeUpdates = std::max(1U, static_cast<std::uint32_t>(std::floor(kFadeSteps / speed)));
    const auto rate = static_cast<std::uint32_t>(std::max(0.0F, std::floor(settings.fadeRate)));
    m_alphaStep = static_cast<std::uint8_t>(((settings.colour[3] / m_fadeUpdates) * rate) & 0xFFU);
    m_baseInterval = kUpdateTicks;
    if (m_alphaStep == 0) {
        m_alphaStep = 1;
        m_baseInterval = kSlowUpdateTicks;
        m_fadeUpdates = static_cast<std::uint32_t>(std::floor(kSlowFadeSteps / speed));
    }
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
    // Its size, from 0 at birth to the random size at its first update.
    wisp.size = kMinSize + ((kMaxSize - kMinSize) * unit());
    wisp.interval = m_baseInterval;
    return wisp;
}

bool GroundFog::update(Wisp& wisp, const EffectsViewer& viewer) const {
    CONEY_ASSERT(m_settings.has_value()); // wisps exist only once the fog is set
    // The shared particle step: the current alpha and size become the previous ones.
    wisp.previousAlpha = wisp.alpha;
    wisp.previousSize = wisp.size;
    // 1. Fade in while young and short of the colour's alpha.
    const std::uint8_t target = m_settings->colour[3];
    if (wisp.age < m_fadeUpdates && wisp.alpha < target) {
        wisp.alpha = static_cast<std::uint8_t>(std::min<int>(wisp.alpha + m_alphaStep, target));
    }
    ++wisp.age;
    // 2. Hide: far from the camera or out of its view (updating every tick), or too near (every 30 ticks).
    const float distance = std::sqrt(distanceSquared(wisp.position, viewer.position));
    wisp.hidden = false;
    wisp.interval = m_baseInterval;
    if (distance > kRadius ||
        (viewer.window && !nearView(viewer.position, *viewer.window, wisp.position, kViewMargin))) {
        wisp.hidden = true;
        wisp.interval = kHiddenUpdateTicks;
    } else if (distance < kHideWithin) {
        wisp.hidden = true;
        wisp.interval = kSlowUpdateTicks;
    }
    if (wisp.hidden) {
        wisp.alpha = 0;
    }
    // 3. End: from its third update, a wisp that was hidden at its last.
    return wisp.age < kEndFromUpdate || wisp.previousAlpha != 0;
}

void GroundFog::step(float seconds, const EffectsViewer& viewer) {
    if (!m_settings) {
        return;
    }
    // Top the view up when a top-up is due.
    m_frames += seconds * kFramesPerSecond;
    if (m_frames >= static_cast<float>(kTopUpFrames)) {
        m_frames = std::fmod(m_frames - static_cast<float>(kTopUpFrames), static_cast<float>(kTopUpFrames));
        const std::size_t room = m_maxWisps > m_wisps.size() ? m_maxWisps - m_wisps.size() : 0;
        for (std::size_t i = 0; i < std::min(room, kWispsPerTopUp); ++i) {
            m_wisps.push_back(makeWisp(viewer, m_settings->drift));
        }
    }
    // Drift, then run the whole ticks of the step: each wisp updates when its interval is up.
    for (Wisp& wisp : m_wisps) {
        wisp.position = anim::add(wisp.position, anim::scale(wisp.velocity, seconds));
    }
    m_ticks += seconds * kFramesPerSecond;
    for (; m_ticks >= 1.0F; m_ticks -= 1.0F) {
        std::erase_if(m_wisps, [this, &viewer](Wisp& wisp) {
            wisp.sinceUpdate += 1.0F;
            if (wisp.sinceUpdate < static_cast<float>(wisp.interval)) {
                return false;
            }
            wisp.sinceUpdate = 0.0F;
            return !update(wisp, viewer);
        });
    }
}

ParticleSheet GroundFog::sheetOf(std::uint32_t sprite) {
    constexpr std::uint32_t kFog01Record = 0x213;
    return (sprite >> 16U) == kFog01Record ? ParticleSheet::PartFog01 : ParticleSheet::PartFog00;
}

std::vector<GroundFog::Drawn> GroundFog::drawn() const {
    std::vector<Drawn> out;
    if (!m_settings) {
        return out;
    }
    // The batch takes the first kDrawnPerFrame wisps; hidden ones take their place but show nothing.
    const std::size_t count = std::min(m_wisps.size(), kDrawnPerFrame);
    for (std::size_t i = 0; i < count; ++i) {
        const Wisp& wisp = m_wisps.at(i);
        // Blended from the last update's values by the share of its interval gone (Colour_LerpRatio).
        const float t = std::clamp((wisp.sinceUpdate + m_ticks) / static_cast<float>(wisp.interval), 0.0F, 1.0F);
        const float alpha = static_cast<float>(wisp.previousAlpha) +
                            ((static_cast<float>(wisp.alpha) - static_cast<float>(wisp.previousAlpha)) * t);
        const auto byte = static_cast<std::uint8_t>(std::clamp(std::lround(alpha), 0L, 255L));
        if (byte == 0) {
            continue;
        }
        const float size = wisp.previousSize + ((wisp.size - wisp.previousSize) * t);
        out.push_back(Drawn{wisp.position, packed(m_settings->colour, byte), kDrawnScale * size});
    }
    return out;
}

} // namespace coney::effects
