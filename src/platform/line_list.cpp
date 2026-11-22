// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the debug tools' scene lines (docs/guides/debug-menu.md); the original has none
#include "platform/line_list.h"

#include <rw.h>

#include "platform/draw_calls.h"
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
        m_points.push_back(Point{p.x, p.y, p.z, colour});
    }
}

void LineList::draw(RenderEngine& /*engine*/) {
    if (m_points.empty()) {
        return;
    }
    std::vector<rw::gl3::Im3DVertex> vertices;
    vertices.reserve(m_points.size());
    for (const Point& point : m_points) {
        rw::gl3::Im3DVertex vertex{};
        vertex.setX(point.x);
        vertex.setY(point.y);
        vertex.setZ(point.z);
        vertex.setColor(point.colour.r, point.colour.g, point.colour.b, point.colour.a);
        vertices.push_back(vertex);
    }
    draw::setRenderState(rw::ZTESTENABLE, 1);
    draw::setRenderState(rw::ZWRITEENABLE, 0);
    draw::setRenderState(rw::VERTEXALPHA, 0);
    draw::setRenderStatePtr(rw::TEXTURERASTER, nullptr);
    draw::im3dTransform(vertices.data(), static_cast<rw::int32>(vertices.size()), nullptr,
                        rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA);
    draw::im3dRenderPrimitive(rw::PRIMTYPELINELIST);
    draw::im3dEnd();
    draw::setRenderState(rw::ZWRITEENABLE, 1);
}

} // namespace coney::platform
