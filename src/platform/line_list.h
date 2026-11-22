// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the debug tools' scene lines (docs/guides/debug-menu.md); the original has none
#pragma once

#include <vector>

#include "animation/anim_math.h"
#include "graphics/render_device.h"

namespace coney::platform {

class RenderEngine;

/// A coloured line list for the 3D scene, in the game's axes: the debug tools' lines (collision, paths, the picked
/// thing), drawn tested against depth, not writing it, untextured.
class LineList {
  public:
    /// Adds a line from `a` to `b` (game axes) in `colour`.
    void add(anim::Vec3 a, anim::Vec3 b, graphics::Rgba colour);
    /// Draws the lines onto `engine`'s frame through the current camera; nothing for none.
    void draw(RenderEngine& engine);

  private:
    // One end of a line, in RenderWare's axes, lifted off what it lies on.
    struct Point {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        graphics::Rgba colour;
    };

    std::vector<Point> m_points;
};

} // namespace coney::platform
