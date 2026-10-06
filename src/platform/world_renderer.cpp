// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include <rw.h>

#include "platform/level_file.h"
#include "world/streamed_world.h"

namespace coney::platform {

namespace {

// librw's vector from ours.
rw::V3d toRw(world::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

// Places the camera at `view` (at `position` instead when given) and sets its clip planes `nearClip` and `farClip`,
// fog plane and view window. librw's GL3 renderer flips the camera frame's x axis, so the frame's `right` is the
// screen's left: -pose.right, which keeps the frame right-handed.
void placeCamera(rw::Camera* camera, const WorldView& view, world::Vec3 position, float nearClip, float farClip) {
    rw::Matrix matrix;
    matrix.setIdentity();
    const world::Vec3 r = view.pose.right;
    matrix.right = rw::V3d{-r.x, -r.y, -r.z};
    matrix.up = toRw(view.pose.up);
    matrix.at = toRw(view.pose.forward);
    matrix.pos = toRw(position);
    matrix.update(); // no longer the identity setIdentity() marked it as
    matrix.optimize();
    camera->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
    camera->setNearPlane(nearClip);
    camera->setFarPlane(farClip);
    camera->fogPlane = farClip * view.fogStart;
    const rw::V2d window{view.halfWidth, view.halfHeight};
    camera->setViewWindow(&window);
}

// The camera as the world pass uses it: at the view, from the near clip to the draw distance.
void placeCamera(rw::Camera* camera, const WorldView& view) {
    placeCamera(camera, view, view.pose.position, view.nearClip, view.drawDistance);
}

// Moves the camera between passes: new placement and planes, taking effect for what is drawn next (librw computes the
// projection when an update begins). Beginning an update also makes the camera's own world current, which is none,
// so the world whose lights librw uses is put back afterwards.
void replaceCamera(rw::Camera* camera, const WorldView& view, world::Vec3 position, float nearClip, float farClip) {
    rw::World* lights = rw::engine->currentWorld;
    camera->endUpdate();
    placeCamera(camera, view, position, nearClip, farClip);
    camera->beginUpdate();
    rw::engine->currentWorld = lights;
}

// The atomic of a level object's part, or null.
rw::Atomic* atomicOf(const chunk::LoadedObject* object) { return levelAtomic(object); }

} // namespace

void placeWorldCamera(rw::Camera* camera, const WorldView& view) { placeCamera(camera, view); }

world::FrameMatrix cloudFrame(const world::FrameMatrix& base, std::uint64_t nowMs) {
    // Each row, and the position, turned about y: (x, y, z) -> (x cos a + z sin a, y, -x sin a + z cos a).
    const float angle = static_cast<float>(nowMs % 377'000'000ULL) * kCloudRadiansPerMs; // a whole number of turns
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const auto turn = [c, s](world::Vec3 v) { return world::Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c}; };
    return world::FrameMatrix{
        .right = turn(base.right), .up = turn(base.up), .at = turn(base.at), .position = turn(base.position)};
}

void WorldRenderer::renderBackground(rw::Camera* camera, const world::LevelObject& level, const WorldView& view,
                                     graphics::Rgba fogColour, float pendingDistance, std::uint64_t nowMs) const {
    // 1-2. The background is lit by the world's ambient and directional lights, no point lights.
    SceneLighting& lit = lighting();
    // 3. The sky box, then the turning cloud box, round the camera with its translation zeroed: near 0.05, far 5;
    // Z write and fog off, nothing culled.
    replaceCamera(camera, view, world::Vec3{}, kSkyNearClip, kSkyFarClip);
    rw::SetRenderState(rw::ZWRITEENABLE, 0);
    rw::SetRenderState(rw::FOGENABLE, 0);
    rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
    if (rw::Atomic* sky = atomicOf(level.skyBox.model.get()); sky != nullptr) {
        lit.drawBackgroundAtomic(sky);
    }
    if (auto* clouds = dynamic_cast<LevelAtomicObject*>(level.cloudBox.model.get()); clouds != nullptr) {
        clouds->place(cloudFrame(clouds->frame(), nowMs));
        lit.drawBackgroundAtomic(clouds->atomic());
    }
    // 4. The skyline in place, from 39 (or the nearest missing scenery, if nearer) to 560, with Z write on and fog
    // off; then Z alone is cleared so the world, with its much shorter far clip, covers it where it has geometry.
    replaceCamera(camera, view, view.pose.position, std::min(kSkylineNearClip, pendingDistance), kSkylineFarClip);
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    if (rw::Atomic* skyline = atomicOf(level.skyline.model.get()); skyline != nullptr) {
        lit.drawBackgroundAtomic(skyline);
    }
    rw::SetRenderState(rw::FOGENABLE, 1);
    rw::RGBA clearColour = rw::makeRGBA(fogColour.r, fogColour.g, fogColour.b, fogColour.a); // librw takes it non-const
    camera->clear(&clearColour, rw::Camera::CLEARZ);
    // 5. The world pass's camera again.
    replaceCamera(camera, view, view.pose.position, view.nearClip, view.drawDistance);
}

WorldRenderer::WorldRenderer()
    : m_ownLevel(std::make_unique<graphics::LevelLighting>()),
      m_ownLighting(std::make_unique<SceneLighting>(*m_ownLevel, std::nullopt, std::nullopt)) {}

WorldRenderer::~WorldRenderer() = default;

void WorldRenderer::renderSectorAtomic(rw::Atomic* atomic, std::uint64_t fadeEndMs, std::uint64_t nowMs) const {
    // Every material's alpha follows the fade, written only when it changes, as the original does.
    const std::uint8_t alpha = world::fadeInAlpha(fadeEndMs, nowMs);
    rw::Geometry* geometry = atomic->geometry;
    for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
        rw::Material* material = geometry->matList.materials[i];
        if (material->color.alpha != alpha) {
            material->color.alpha = alpha;
        }
    }
    lighting().drawWorldAtomic(atomic);
}

void WorldRenderer::render(RenderEngine& engine, const WorldSet& set, const world::LevelObject* level,
                           const WorldView& given, float pendingDistance, std::uint64_t nowMs,
                           const std::function<void()>& drawObjects, const std::function<void()>& overlay) {
    m_drawn = 0;
    // The level's fog: its colour clears the frame, and it starts at its fraction of the draw distance.
    SceneLighting& lit = lighting();
    const graphics::Rgba fogColour = lit.fog().colour;
    WorldView view = given;
    view.fogStart = lit.fog().start;
    // 1-4. The camera, with the draw distance as its far clip and the fog from half of it. The frame is begun first:
    // a resized window gets a new camera there, which is then set up and begun again.
    engine.beginWindowFrame(fogColour);
    rw::Camera* camera = engine.camera();
    if (camera == nullptr) {
        // NULL backend: nothing to draw but the overlay's calls, which draw nothing either.
        if (overlay) {
            overlay();
        }
        engine.present();
        return;
    }
    placeCamera(camera, view);
    camera->beginUpdate();
    // The light manager's viewport pass: the ambients' colours, the cull and the coronas of this frame.
    lit.beginFrame(view.pose, view.nearClip, view.drawDistance, nowMs);

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

    // The level's background, before the world (LevelObject_RenderBackground, from the viewport pass).
    if (level != nullptr) {
        renderBackground(camera, *level, view, fogColour, pendingDistance, nowMs);
        // 5. The level world, the light glows: nothing culled, Z test and write and fog on.
        if (rw::Atomic* glows = atomicOf(level->levelWorld.get()); glows != nullptr) {
            rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
            lit.drawBackgroundAtomic(glows);
            ++m_drawn;
        }
        rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    }

    // 6. The `s` world. 7. The objects, when there are any. 8. The `d` world, the same way as the `s` world.
    const std::span<world::StreamedWorld* const> worlds = set.worlds();
    const world::ViewFrustum frustum(view.pose, view.halfWidth, view.halfHeight, view.nearClip, view.drawDistance);
    for (std::size_t w = 0; w < worlds.size(); ++w) {
        if (w == 1 && drawObjects) {
            drawObjects();
            // Put back what the objects may have changed: the world pass's render states.
            rw::SetRenderState(rw::ZTESTENABLE, 1);
            rw::SetRenderState(rw::ZWRITEENABLE, 1);
            rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
            rw::SetRenderState(rw::FOGENABLE, 1);
            rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
            rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        }
        const std::vector<world::StreamedSector>& sectors = worlds[w]->sectors();
        for (const std::uint32_t sector : worlds[w]->collectSectorsIn(view.pose.position, frustum)) {

            if (rw::Atomic* atomic = set.atomic(w, sector); atomic != nullptr) {
                renderSectorAtomic(atomic, sectors[sector].fadeEndMs, nowMs);
                ++m_drawn;
            }
        }
    }
    // A single world (objarena): the objects come after it.
    if (worlds.size() < 2 && drawObjects) {
        drawObjects();
    }
    // The coronas of the visible lights, over the world.
    lit.drawCoronas();
    rw::SetRenderState(rw::FOGENABLE, 0);
    // The 2D pass over the world (the menus or the HUD): no depth test, nothing culled.
    if (overlay) {
        rw::SetRenderState(rw::ZTESTENABLE, 0);
        rw::SetRenderState(rw::ZWRITEENABLE, 0);
        rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
        overlay();
    }
    engine.present();
}

} // namespace coney::platform
