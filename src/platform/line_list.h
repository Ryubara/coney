// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the debug tools' scene lines (docs/guides/debug-menu.md); the original has none
#pragma once

#include <vector>

#include "animation/anim_math.h"
#include "graphics/frame_description.h"
#include "graphics/render_device.h"
#include "world/world_streams.h"

namespace coney::platform {

class RenderEngine;

/// A debug line's end `game` (game axes) in RenderWare's axes, lifted 0.02 m so a line on a surface is not hidden in
/// it: the place LineList draws it, and the one the scene pass's lines take (graphics::SceneLineVertex).
[[nodiscard]] world::Vec3 lineEndOf(anim::Vec3 game);

/// A coloured line list for the 3D scene, in the game's axes: the debug tools' lines (collision, paths, the picked
/// thing), drawn as one effect draw of the frame description (RenderEngine::drawEffect()): tested against depth, not
/// writing it, untextured.
class LineList {
  public:
    /// Adds a line from `a` to `b` (game axes) in `colour`.
    void add(anim::Vec3 a, anim::Vec3 b, graphics::Rgba colour);
    /// Draws the lines onto `engine`'s frame through the current camera; nothing for none.
    void draw(RenderEngine& engine);

  private:
    std::vector<graphics::EffectVertex> m_vertices;
};

} // namespace coney::platform
