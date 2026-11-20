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

#include "platform/draw_calls.h"
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
    draw::endCameraUpdate(camera);
    placeCamera(camera, view, position, nearClip, farClip);
    draw::beginCameraUpdate(camera);
    rw::engine->currentWorld = lights;
}

// The frame description's alpha test for librw's ALPHATESTFUNC `value`.
graphics::AlphaTest sceneAlphaTest(rw::uint32 value) {
    switch (value) {
    case rw::ALPHAGREATEREQUAL:
        return graphics::AlphaTest::GreaterEqual;
    case rw::ALPHALESS:
        return graphics::AlphaTest::Less;
    default:
        return graphics::AlphaTest::Always;
    }
}

// The atomic of a level object's part, or null.
rw::Atomic* atomicOf(const chunk::LoadedObject* object) { return levelAtomic(object); }

} // namespace

void placeWorldCamera(rw::Camera* camera, const WorldView& view) { placeCamera(camera, view); }

graphics::SceneCamera sceneCameraOf(const WorldView& view) {
    const auto axis = [](world::Vec3 v) { return std::array<float, 3>{v.x, v.y, v.z}; };
    graphics::SceneCamera camera;
    camera.frame = graphics::Transform3d{.right = axis(view.pose.right),
                                         .up = axis(view.pose.up),
                                         .at = axis(view.pose.forward),
                                         .position = axis(view.pose.position)};
    camera.halfWidth = view.halfWidth;
    camera.halfHeight = view.halfHeight;
    camera.nearClip = view.nearClip;
    camera.farClip = view.drawDistance;
    camera.fogStart = view.drawDistance * view.fogStart; // placeCamera()'s fog plane
    return camera;
}

world::FrameMatrix cloudFrame(const world::FrameMatrix& base, std::uint64_t nowMs) {
    // Each row, and the position, turned by -a about y, as the original's camera turned by +a shows the model:
    // (x, y, z) -> (x cos a - z sin a, y, x sin a + z cos a), clockwise seen from above.
    const float angle = static_cast<float>(nowMs % 377'000'000ULL) * kCloudRadiansPerMs; // a whole number of turns
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const auto turn = [c, s](world::Vec3 v) { return world::Vec3{v.x * c - v.z * s, v.y, v.x * s + v.z * c}; };
    return world::FrameMatrix{
        .right = turn(base.right), .up = turn(base.up), .at = turn(base.at), .position = turn(base.position)};
}

void WorldRenderer::renderBackground(rw::Camera* camera, const world::LevelObject& level, const WorldView& view,
                                     graphics::Rgba fogColour, float pendingDistance, std::uint64_t nowMs) const {
    // 1-2. The background is lit by the world's ambient and directional lights, no point lights.
    // The original's two switches for the sky and the skyline are always 1 (level-loading.md#render-order), so their
    // off branches, which only set the render states, are dead code and not ported.
    SceneLighting& lit = lighting();
    // 3. The sky box, then the turning cloud box, round the camera with its translation zeroed: near 0.05, far 5;
    // Z write and fog off, nothing culled.
    replaceCamera(camera, view, world::Vec3{}, kSkyNearClip, kSkyFarClip);
    draw::setRenderState(rw::ZWRITEENABLE, 0);
    draw::setRenderState(rw::FOGENABLE, 0);
    draw::setRenderState(rw::CULLMODE, rw::CULLNONE);
    if (rw::Atomic* sky = atomicOf(level.skyBox.model.get()); sky != nullptr) {
        lit.drawBackgroundAtomic(sky);
    }
    if (auto* clouds = dynamic_cast<LevelAtomicObject*>(level.cloudBox.model.get()); clouds != nullptr) {
        clouds->place(cloudFrame(clouds->frame(), nowMs));
        lit.drawBackgroundAtomic(clouds->atomic());
    }
    // 4. The skyline in place, from 39 (or the nearest missing scenery, if nearer) to 560, with Z write on and fog
    // off; then Z alone is cleared so the world, with its much shorter far clip, covers it where it has geometry.
    // Stand-in: the original also tells the PS2 driver the camera's far clip here, which no PC renderer has.
    replaceCamera(camera, view, view.pose.position, std::min(kSkylineNearClip, pendingDistance), kSkylineFarClip);
    draw::setRenderState(rw::ZWRITEENABLE, 1);
    if (rw::Atomic* skyline = atomicOf(level.skyline.model.get()); skyline != nullptr) {
        lit.drawBackgroundAtomic(skyline);
    }
    draw::setRenderState(rw::FOGENABLE, 1);
    rw::RGBA clearColour = rw::makeRGBA(fogColour.r, fogColour.g, fogColour.b, fogColour.a); // librw takes it non-const
    draw::clearCamera(camera, &clearColour, rw::Camera::CLEARZ);
    // 5. The world pass's camera again (stand-in: there is no PS2 driver far clip to put back).
    replaceCamera(camera, view, view.pose.position, view.nearClip, view.drawDistance);
}

WorldRenderer::WorldRenderer()
    : m_ownLevel(std::make_unique<graphics::LevelLighting>()),
      m_ownLighting(std::make_unique<SceneLighting>(*m_ownLevel, std::nullopt, std::nullopt)) {}

// Frees the recording camera and its frame, made on first use by recordingCamera().
WorldRenderer::~WorldRenderer() {
    if (m_recordingCamera != nullptr) {
        rw::Frame* frame = m_recordingCamera->getFrame();
        m_recordingCamera->setFrame(nullptr);
        if (frame != nullptr) {
            frame->destroy();
        }
        m_recordingCamera->destroy();
    }
}

rw::Camera* WorldRenderer::recordingCamera() {
    if (m_recordingCamera == nullptr) {
        m_recordingCamera = rw::Camera::create();
        m_recordingCamera->setFrame(rw::Frame::create());
    }
    return m_recordingCamera;
}

void WorldRenderer::renderSectorAtomic(RenderEngine& engine, rw::Atomic* atomic, const WorldSet::SectorMesh* mesh,
                                       const graphics::SceneStates& states, std::uint64_t fadeEndMs,
                                       std::uint64_t nowMs) const {
    const std::uint8_t alpha = world::fadeInAlpha(fadeEndMs, nowMs);
    // The frame description's draw: the meshes by id, where the atomic is, its fade, the world pass's states and the
    // lights the light manager selects for its sphere; the GL3 renderer writes the fade and draws it.
    if (mesh != nullptr && mesh->mesh.valid() && !lighting().meshViewsOn()) {
        const rw::Matrix* ltm = atomic->getFrame()->getLTM();
        const auto axis = [](const rw::V3d& v) { return std::array<float, 3>{v.x, v.y, v.z}; };
        graphics::Draw3d draw;
        draw.mesh = mesh->mesh;
        draw.dualLayer = mesh->dualLayer;
        draw.transform = graphics::Transform3d{
            .right = axis(ltm->right), .up = axis(ltm->up), .at = axis(ltm->at), .position = axis(ltm->pos)};
        draw.material = graphics::Material3d{.fade = alpha, .distanceLevels = true};
        draw.states = states;
        const std::vector<graphics::SceneLight> lights = lighting().worldLights(atomic);
        engine.drawScene(draw, lights);
        return;
    }
    // Every material's alpha follows the fade, written only when it changes, as the original does.
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
                           const std::function<void()>& drawObjects, const std::function<void()>& overlay,
                           const std::function<void()>& afterWorlds) {
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
    if (camera == nullptr && (engine.backend() == RenderBackend::Gpu || m_recordHeadless)) {
        // The GPU backend, which has no librw camera and draws the frame description through its own
        // (docs/engine/gpu-renderer.md#scene), or the NULL backend drawing a synthetic scene for the tests
        // (docs/engine/renderer.md#comparing): a camera of the renderer's own with no buffers, so the world's draws are
        // added, and recorded while the draw layer records, as a window would draw them.
        camera = recordingCamera();
    }
    if (camera == nullptr) {
        // NULL backend: nothing to draw but the overlay's calls, which draw nothing either.
        if (overlay) {
            overlay();
        }
        engine.present();
        return;
    }
    placeCamera(camera, view);
    draw::beginCameraUpdate(camera);
    // The animations' time: game time, or the time freezeAnimation() holds them at.
    const std::uint64_t animationMs = m_animationMs.value_or(nowMs);
    // The light manager's viewport pass: the ambients' colours, the cull and the coronas of this frame.
    lit.beginFrame(view.pose, view.nearClip, view.drawDistance, animationMs);

    // The render states of the streamed worlds' passes: Z test and write, back faces culled, fog in the background
    // colour, blending by alpha for the fade.
    draw::setRenderState(rw::ZTESTENABLE, 1);
    draw::setRenderState(rw::ZWRITEENABLE, 1);
    draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
    draw::setRenderState(rw::FOGENABLE, 1);
    // librw packs the fog colour red in the low byte.
    draw::setRenderState(rw::FOGCOLOR,
                         static_cast<rw::uint32>(fogColour.r) | static_cast<rw::uint32>(fogColour.g) << 8U |
                             static_cast<rw::uint32>(fogColour.b) << 16U | static_cast<rw::uint32>(fogColour.a) << 24U);
    draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);

    // The level's background, before the world (LevelObject_RenderBackground, from the viewport pass).
    if (level != nullptr && !m_sceneOnly) {
        renderBackground(camera, *level, view, fogColour, pendingDistance, animationMs);
        // 5. The level world, the light glows: nothing culled, Z test and write and fog on.
        if (rw::Atomic* glows = atomicOf(level->levelWorld.get()); glows != nullptr) {
            draw::setRenderState(rw::CULLMODE, rw::CULLNONE);
            lit.drawBackgroundAtomic(glows);
            ++m_drawn;
        }
        draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
    }

    // The world pass's camera, for a renderer that draws the frame description through its own (the GPU renderer).
    engine.setSceneCamera(sceneCameraOf(view));
    // 6. The `s` world. 7. The objects, when there are any. 8. The `d` world, the same way as the `s` world. The
    // sectors' draws carry the world pass's states, with the alpha test as it stands at each.
    graphics::SceneStates states;
    states.fogColour = fogColour;
    const std::span<world::StreamedWorld* const> worlds = set.worlds();
    // Stand-in: the level's occluders and the PVS are not consulted, since they only save work
    // (docs/research/world.md#visibility), and no camera is added to or taken from each world, which RenderWare needs
    // and librw does not.
    const world::ViewFrustum frustum(view.pose, view.halfWidth, view.halfHeight, view.nearClip, view.drawDistance);
    for (std::size_t w = 0; w < worlds.size(); ++w) {
        if (w == 1 && drawObjects) {
            drawObjects();
            // Put back what the objects may have changed: the world pass's render states.
            draw::setRenderState(rw::ZTESTENABLE, 1);
            draw::setRenderState(rw::ZWRITEENABLE, 1);
            draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
            draw::setRenderState(rw::FOGENABLE, 1);
            draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
            draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        }
        const std::vector<world::StreamedSector>& sectors = worlds[w]->sectors();
        for (const std::uint32_t sector : worlds[w]->collectSectorsIn(view.pose.position, frustum)) {

            if (rw::Atomic* atomic = set.atomic(w, sector); atomic != nullptr) {
                states.alphaTest = sceneAlphaTest(draw::getRenderState(rw::ALPHATESTFUNC));
                renderSectorAtomic(engine, atomic, set.sectorMesh(w, sector), states, sectors[sector].fadeEndMs, nowMs);
                ++m_drawn;
            }
        }
    }
    // A single world (objarena): the objects come after it.
    if (worlds.size() < 2 && drawObjects) {
        drawObjects();
    }
    // 9-10. The water and the see-through parts, in the world pass's states.
    if (afterWorlds) {
        afterWorlds();
    }
    // The coronas of the visible lights, over the world.
    if (!m_sceneOnly) {
        lit.drawCoronas();
    }
    draw::setRenderState(rw::FOGENABLE, 0);
    // The 2D pass over the world (the menus or the HUD): no depth test, nothing culled.
    if (overlay) {
        draw::setRenderState(rw::ZTESTENABLE, 0);
        draw::setRenderState(rw::ZWRITEENABLE, 0);
        draw::setRenderState(rw::CULLMODE, rw::CULLNONE);
        overlay();
    }
    engine.present();
}

} // namespace coney::platform
