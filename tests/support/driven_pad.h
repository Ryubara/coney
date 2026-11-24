// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Player 1's pad as a disc test steers it frame by frame, as a player reading the screen would: the buttons held and
// both analog sticks, and the left-stick deflection that walks him toward a point under the camera shown. No game
// data.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "platform/play_level_mode.h"

namespace coney::test {

/// Player 1's pad, set by the test each frame: the buttons held and the two sticks in [-1, 1] with y up.
class DrivenPad final : public InputSource {
  public:
    std::uint16_t buttons = 0;
    float rightX = 0.0F;
    float rightY = 0.0F;
    float leftX = 0.0F;
    float leftY = 0.0F;

    /// Port 0 connected with this frame's buttons and sticks; the other ports idle.
    PortSamples sample(std::uint64_t /*frame*/) override {
        PortSamples samples{};
        PadSample& pad = samples[0];
        pad.connected = true;
        pad.buttons = buttons;
        pad.sticks[0] = raw(rightX);
        pad.sticks[1] = raw(-rightY);
        pad.sticks[2] = raw(leftX);
        pad.sticks[3] = raw(-leftY);
        return samples;
    }

  private:
    // The raw byte for `value` as an input script writes it, past each axis's dead zone (core/pad.h).
    static std::uint8_t raw(float value) {
        return stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }
};

/// The left stick, in [-1, 1] at `deflection`, that walks player 1 toward `to` under the camera shown: the stick is
/// read against the camera's forward, x to its right.
inline std::array<float, 2> stickTowards(const platform::PlayLevelMode& play, anim::Vec3 to, float deflection) {
    const anim::Vec3 way = anim::subtract(to, play.player().human().position());
    anim::Vec3 forward = anim::subtract(play.cameraTarget(), play.cameraEye());
    const float length = std::hypot(forward.x, forward.y);
    const float along = std::hypot(way.x, way.y);
    if (length <= 0.0F || along <= 0.0F) {
        return {0.0F, 0.0F};
    }
    forward = {forward.x / length, forward.y / length, 0.0F};
    const anim::Vec3 right{forward.y, -forward.x, 0.0F};
    return {deflection * ((way.x * right.x) + (way.y * right.y)) / along,
            deflection * ((way.x * forward.x) + (way.y * forward.y)) / along};
}

} // namespace coney::test
