// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_renderer.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <rw.h>

#include "world/streamed_world.h"

namespace coney::platform {

namespace {

// librw's vector from ours.
rw::V3d toRw(world::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

// Places the camera at `view` and sets its clip planes, fog plane and view window. librw's GL3 renderer flips the
// camera frame's x axis, so the frame's `right` is the screen's left: -pose.right, which keeps the frame right-handed.
void placeCamera(rw::Camera* camera, const WorldView& view) {
    rw::Matrix matrix;
    matrix.setIdentity();
    const world::Vec3 r = view.pose.right;
    matrix.right = rw::V3d{-r.x, -r.y, -r.z};
    matrix.up = toRw(view.pose.up);
    matrix.at = toRw(view.pose.forward);
    matrix.pos = toRw(view.pose.position);
    matrix.update(); // no longer the identity setIdentity() marked it as
    matrix.optimize();
    camera->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
    camera->setNearPlane(view.nearClip);
    camera->setFarPlane(view.drawDistance);
    camera->fogPlane = view.drawDistance * kFogStart;
    const rw::V2d window{view.halfWidth, view.halfHeight};
    camera->setViewWindow(&window);
}

} // namespace

WorldRenderer::WorldRenderer(float ambient)
    : m_lights(rw::World::create()), m_ambient(rw::Light::create(rw::Light::AMBIENT)) {
    m_ambient->setColor(ambient, ambient, ambient);
    m_lights->addLight(m_ambient);
}

WorldRenderer::~WorldRenderer() {
    m_lights->removeLight(m_ambient);
    m_ambient->destroy();
    m_lights->destroy();
}

void WorldRenderer::renderSectorAtomic(rw::Atomic* atomic, std::uint64_t fadeEndMs, std::uint64_t nowMs) {
    // Every material's alpha follows the fade, written only when it changes, as the original does.
    const std::uint8_t alpha = world::fadeInAlpha(fadeEndMs, nowMs);
    rw::Geometry* geometry = atomic->geometry;
    for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
        rw::Material* material = geometry->matList.materials[i];
        if (material->color.alpha != alpha) {
            material->color.alpha = alpha;
        }
    }
    atomic->render();
}

void WorldRenderer::render(RenderEngine& engine, const WorldSet& set, const WorldView& view, graphics::Rgba fogColour,
                           std::uint64_t nowMs) {
    m_drawn = 0;
    // 1-4. The camera, with the draw distance as its far clip and the fog from half of it. The frame is begun first:
    // a resized window gets a new camera there, which is then set up and begun again.
    engine.beginWindowFrame(fogColour);
    rw::Camera* camera = engine.camera();
    if (camera == nullptr) {
        engine.present(); // NULL backend: nothing to draw
        return;
    }
    placeCamera(camera, view);
    camera->beginUpdate();
    // librw lights an atomic from the current world's lights; give it the empty one.
    rw::engine->currentWorld = m_lights;

    // The render states of the streamed worlds' passes: Z test and write, back faces culled, fog in the background
    // colour, blending by alpha for the fade.
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    rw::SetRenderState(rw::FOGENABLE, 1);
    // librw packs the fog colour red in the low byte.
    rw::SetRenderState(rw::FOGCOLOR, static_cast<rw::uint32>(fogColour.r) | static_cast<rw::uint32>(fogColour.g) << 8U |
                                         static_cast<rw::uint32>(fogColour.b) << 16U |
                                         static_cast<rw::uint32>(fogColour.a) << 24U);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);

    // 5. The level world: not loaded yet. 6. The `s` world. 7. Objects: none yet. 8. The `d` world, the same way.
    const std::span<world::StreamedWorld* const> worlds = set.worlds();
    for (std::size_t w = 0; w < worlds.size(); ++w) {
        const std::vector<world::StreamedSector>& sectors = worlds[w]->sectors();
        for (const std::uint32_t sector : worlds[w]->collectSectors(view.pose.position)) {
            if (rw::Atomic* atomic = set.atomic(w, sector); atomic != nullptr) {
                renderSectorAtomic(atomic, sectors[sector].fadeEndMs, nowMs);
                ++m_drawn;
            }
        }
    }
    rw::SetRenderState(rw::FOGENABLE, 0);
    engine.present();
}

} // namespace coney::platform
