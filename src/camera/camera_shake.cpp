// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/camera_shake.h"

#include <algorithm>
#include <cstddef>

namespace coney::camera {

namespace {

// The rumble's threshold above its base, and its cap above the base.
constexpr int kRumbleThreshold = 0x28;
constexpr int kRumbleCap = 0x60;

// A random share in [-1, 1] from `random`.
float randomShare(GameRandom& random) { return static_cast<float>(random.range(-1000, 1000)) / 1000.0F; }

} // namespace

void CameraShake::start(int level, bool combat) {
    if (level == 0) {
        *this = CameraShake{};
        return;
    }
    if (level < 1 || level > static_cast<int>(kShakeLevels.size())) {
        return;
    }
    const ShakeLevel& shake = kShakeLevels.at(static_cast<std::size_t>(level - 1));
    m_target = shake.amplitude * (combat ? kCombatScale : 1.0F);
    m_time = shake.seconds;
    m_rumbleBase = shake.rumbleBase;
    m_combat = combat;
}

void CameraShake::update(float dt, float step) {
    // The amplitude toward the level's while the time lasts, then back to nothing.
    const float goal = m_time > 0.0F ? m_target : 0.0F;
    m_amplitude += (goal - m_amplitude) * kEase;
    m_time = std::max(0.0F, m_time - dt * std::min(1.0F, kStepScale * step));
    // The rumble: the amplitude's share of the level's, above the threshold, up to the cap.
    m_rumble = 0;
    if (m_target > 0.0F) {
        const float share = 255.0F * m_amplitude / m_target;
        const float threshold =
            static_cast<float>((m_rumbleBase >> 2) + kRumbleThreshold) * (m_combat ? kCombatThresholdScale : 1.0F);
        if (share > threshold) {
            m_rumble = static_cast<std::uint8_t>(std::min(share, static_cast<float>(m_rumbleBase + kRumbleCap)));
        }
    }
    // A new random offset scaled by the amplitude.
    const float size = m_amplitude * kOffsetScale;
    m_offset = anim::Vec3{randomShare(m_random) * size, randomShare(m_random) * size, randomShare(m_random) * size};
}

} // namespace coney::camera
