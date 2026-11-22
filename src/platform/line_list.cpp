// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the debug tools' scene lines (docs/guides/debug-menu.md); the original has none
#include "platform/line_list.h"

#include "platform/play_scenery.h"
#include "platform/render_engine.h"

namespace coney::platform {

namespace {

// How far debug lines are lifted off what they lie on, in metres.
constexpr float kLineLift = 0.02F;

} // namespace

void LineList::add(anim::Vec3 a, anim::Vec3 b, graphics::Rgba colour) {
    // One vertex each, lifted a little so a line on a surface is not hidden in it, turned into RenderWare's axes.
    for (const anim::Vec3 game : {a, b}) {
        const world::Vec3 p = toRenderWare(anim::Vec3{game.x, game.y, game.z + kLineLift});
        m_vertices.push_back(graphics::EffectVertex{p.x, p.y, p.z, colour, 0.0F, 0.0F});
    }
}

void LineList::draw(RenderEngine& engine) {
    if (m_vertices.empty()) {
        return;
    }
    graphics::EffectDraw draw;
    draw.shape = graphics::EffectShape::Lines;
    draw.states.depthTest = true;
    draw.states.depthWrite = false;
    draw.states.vertexAlpha = false;
    draw.restore.depthWrite = true;
    engine.drawEffect(draw, m_vertices, {}, nullptr);
}

} // namespace coney::platform
