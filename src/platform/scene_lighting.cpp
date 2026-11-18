// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/scene_lighting.h"

#include <array>
#include <cmath>
#include <utility>

#include <rw.h>

#include "platform/draw_calls.h"
#include "platform/lit_atomic.h"
#include "platform/perf_probes.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_lod.h"
#include "platform/world_atomic.h"

namespace coney::platform {

namespace {

// librw's vector from ours.
rw::V3d toRw(world::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

// The librw texture of a sheet (a SheetTexture), or null.
rw::Texture* rwTextureOf(const std::optional<graphics::SpriteSheet>& sheet) {
    if (!sheet || !sheet->texture) {
        return nullptr;
    }
    const auto* texture = dynamic_cast<const SheetTexture*>(sheet->texture.get());
    return texture != nullptr ? texture->rwTexture() : nullptr;
}

// One textured, coloured vertex for librw's immediate mode. A sprite's colour is RenderWare's 0-255 as given (255 full
// and opaque, docs/research/gui.md#sprite-colours), not the GS's 0x80 scale: a corona's is its light's colour × 255,
// and the blob shadow's alpha 128 is half transparent (docs/research/lighting.md#humans).
rw::gl3::Im3DVertex vertex(rw::V3d p, float u, float v, const std::array<std::uint8_t, 4>& rgba) {
    rw::gl3::Im3DVertex out{};
    out.setX(p.x);
    out.setY(p.y);
    out.setZ(p.z);
    out.setColor(rgba[0], rgba[1], rgba[2], rgba[3]);
    out.setU(u);
    out.setV(v);
    return out;
}

// Appends the two triangles of a quad centred on `centre`, spanned by the half-axes `x` and `y`, showing `uv`.
void addQuad(std::vector<rw::gl3::Im3DVertex>& out, rw::V3d centre, rw::V3d x, rw::V3d y, const graphics::UvRect& uv,
             const std::array<std::uint8_t, 4>& rgba) {
    const auto corner = [&](float sx, float sy) {
        return rw::V3d{centre.x + x.x * sx + y.x * sy, centre.y + x.y * sx + y.y * sy, centre.z + x.z * sx + y.z * sy};
    };
    const rw::gl3::Im3DVertex a = vertex(corner(-1, 1), uv.u0, uv.v0, rgba);
    const rw::gl3::Im3DVertex b = vertex(corner(1, 1), uv.u1, uv.v0, rgba);
    const rw::gl3::Im3DVertex c = vertex(corner(1, -1), uv.u1, uv.v1, rgba);
    const rw::gl3::Im3DVertex d = vertex(corner(-1, -1), uv.u0, uv.v1, rgba);
    out.insert(out.end(), {a, b, c, a, c, d});
}

// Draws `vertices` as triangles with `texture`: Z tested, not written, no fog, no culling, blended by alpha.
void drawQuads(std::vector<rw::gl3::Im3DVertex>& vertices, rw::Texture* texture) {
    if (vertices.empty()) {
        return;
    }
    draw::setRenderState(rw::ZTESTENABLE, 1);
    draw::setRenderState(rw::ZWRITEENABLE, 0);
    draw::setRenderState(rw::FOGENABLE, 0);
    draw::setRenderState(rw::CULLMODE, rw::CULLNONE);
    draw::setRenderState(rw::VERTEXALPHA, 1);
    draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    draw::setRenderStatePtr(rw::TEXTURERASTER, texture != nullptr ? texture->raster : nullptr);
    draw::im3dTransform(vertices.data(), static_cast<rw::int32>(vertices.size()), nullptr,
                        rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA | rw::im3d::VERTEXUV);
    draw::im3dRenderPrimitive(rw::PRIMTYPETRILIST);
    draw::im3dEnd();
    draw::setRenderState(rw::ZWRITEENABLE, 1);
}

// The squared distance between two points.
float distanceSq(world::Vec3 a, world::Vec3 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// An atomic's bounding sphere in the world, RenderWare's axes.
graphics::LightSphere sphereOf(rw::Atomic* atomic) {
    const rw::Sphere* sphere = atomic->getWorldBoundingSphere();
    return graphics::LightSphere{{sphere->center.x, sphere->center.y, sphere->center.z}, sphere->radius};
}

} // namespace

SceneLighting::SceneLighting(graphics::LevelLighting& lighting, std::optional<graphics::SpriteSheet> coronas,
                             std::optional<graphics::SpriteSheet> shadows)
    : m_lighting(lighting), m_coronaSheet(std::move(coronas)), m_shadowSheet(std::move(shadows)),
      m_world(rw::World::create()), m_mirrors(graphics::kLightCapacity, nullptr) {}

// Takes the rig's lights and world down.
SceneLighting::~SceneLighting() {
    // Lights out of the world first, then each light and its frame, which Light::destroy leaves alone.
    for (rw::Light* light : m_inWorld) {
        m_world->removeLight(light);
    }
    for (rw::Light* light : m_mirrors) {
        if (light == nullptr) {
            continue;
        }
        rw::Frame* frame = light->getFrame();
        light->destroy();
        if (frame != nullptr) {
            frame->destroy();
        }
    }
    m_world->destroy();
}

void SceneLighting::beginFrame(const world::CameraPose& pose, float nearClip, float farClip, std::uint64_t nowMs) {
    const graphics::LightView drawn{
        .position = pose.position, .forward = pose.forward, .nearClip = nearClip, .farClip = farClip};
    // The lighting lab's Light from and Hold lighting choose the view and the time the lights are culled at; without
    // its switches, the frame's own camera and time.
    const bool fromGame = m_switches != nullptr && m_switches->lightFromGameCamera && m_gameView.has_value();
    const graphics::LitMoment lit =
        m_hold.choose(graphics::LitMoment{.view = fromGame ? *m_gameView : drawn, .nowMs = nowMs},
                      m_switches != nullptr && m_switches->hold);
    // The rig's camera stays the one drawn through: the caps, the coronas' facing, the humans' fade and the objects'
    // cull read it. Only the cull of the lights and the coronas it makes follow Light from and Hold lighting.
    m_pose = pose;
    m_lighting.lights.beginViewport(lit.view, lit.nowMs);
    // The render-side description of the frame's lights, read by drawing only, never by the simulation. The rows of
    // the last frame's selections give their storage to this one's.
    std::vector<graphics::FrameSelection> rows = std::move(m_frame.selections);
    rows.clear();
    m_frame = graphics::buildLightingFrame(m_lighting, lit.view);
    m_frame.coronas = m_lighting.lights.coronas().size();
    m_frame.selections = std::move(rows);
    m_pending.reset();
    if (m_switches != nullptr && !m_switches->layers.coronas) {
        m_lighting.lights.clearCoronas();
    }
}

graphics::SceneLight SceneLighting::sceneLightOf(std::uint16_t index) const {
    const graphics::LightRecord& record = m_lighting.lights.record(index);
    const graphics::LightColour colour =
        m_switches != nullptr ? graphics::layeredColour(m_lighting.lights, index, m_switches->layers) : record.current;
    const world::Vec3 p = record.desc.position;
    const world::Vec3 d = record.desc.direction;
    return graphics::SceneLight{.type = record.desc.type,
                                .colour = {colour.r, colour.g, colour.b},
                                .position = {p.x, p.y, p.z},
                                .direction = {d.x, d.y, d.z},
                                .radius = record.desc.radius,
                                .coneAngle = record.desc.coneAngle};
}

rw::Light* SceneLighting::mirror(std::uint16_t index) {
    const graphics::LightRecord& record = m_lighting.lights.record(index);
    rw::Light*& light = m_mirrors.at(index);
    // A record reused with another type needs a light of that type.
    if (light != nullptr && light->getType() != rwLightType(record.desc.type)) {
        rw::Frame* frame = light->getFrame();
        light->destroy();
        if (frame != nullptr) {
            frame->destroy();
        }
        light = nullptr;
    }
    if (light == nullptr) {
        light = rw::Light::create(rwLightType(record.desc.type));
        light->setFlags(rw::Light::LIGHTATOMICS);
        light->setFrame(rw::Frame::create());
    }
    placeLight(*light, sceneLightOf(index));
    return light;
}

void SceneLighting::useLights(const graphics::LightSelection& selection) {
    for (rw::Light* light : m_inWorld) {
        m_world->removeLight(light);
    }
    m_inWorld.clear();
    m_pending = selection;
    const graphics::LightSelection drawn =
        m_switches != nullptr
            ? graphics::layeredSelection(selection, m_lighting.lights, m_switches->layers, m_switches->view)
            : selection;
    for (const std::uint16_t index : drawn.lights()) {
        rw::Light* light = mirror(index);
        m_world->addLight(light);
        m_inWorld.push_back(light);
    }
}

void SceneLighting::recordSelection(rw::Atomic* atomic) {
    // A second layer of a model (a human's blood pass) adds no row: the original selects once per human
    // (HumanRender_Draw's one call, docs/research/lighting.md#select), and Coney's second selection repeats the first.
    if (!m_pending || atomic == m_meshLayer) {
        m_pending.reset();
        return;
    }
    // The caller from the draw kind the play mode names (cars are drawn among the objects), and the inputs each draw
    // function passes the selection: the world's lists with point lights (none for the background), the objects'
    // with point lights unless a human is hidden, and the squared distance the selection was made at (selectFor()).
    const graphics::LightSphere sphere = sphereOf(atomic);
    const DrawKind kind = currentDrawKind();
    graphics::SelectionCaller caller = graphics::SelectionCaller::Sector;
    if (m_background) {
        caller = graphics::SelectionCaller::Background;
    } else if (kind == DrawKind::Humans) {
        caller = graphics::SelectionCaller::Human;
    } else if (kind == DrawKind::Objects) {
        caller = graphics::SelectionCaller::Object;
    }
    const bool human = caller == graphics::SelectionCaller::Human;
    const bool objects = human || caller == graphics::SelectionCaller::Object;
    const bool pointLights = caller != graphics::SelectionCaller::Background && !(human && m_hiddenHuman);
    m_frame.selections.push_back(graphics::frameSelectionOf(m_lighting.lights, caller, m_pendingDistSq, sphere, objects,
                                                            pointLights, false, human, *m_pending));
    m_pending.reset();
}

void SceneLighting::renderInRig(rw::Atomic* atomic) {
    ++m_atomicsDrawn;
    recordSelection(atomic);
    // Without the mesh views the atomic is drawn as it always is; with them they decide what is drawn, by the kind the
    // caller's scope names (the play mode marks the scenery, the humans and the objects it draws).
    if (m_meshViews == nullptr) {
        renderLit(atomic);
        return;
    }
    m_meshViews->draw(atomic, currentDrawKind(), atomic == m_meshLayer, [this, atomic] { renderLit(atomic); });
}

void SceneLighting::renderLit(rw::Atomic* atomic) { drawLitAtomic(atomic, dualLayerOf(atomic), m_world, m_routed); }

std::vector<graphics::SceneLight> SceneLighting::worldLights(rw::Atomic* atomic) {
    // The selection drawWorldAtomic() makes.
    return lightsOf(selectFor(0.0F, sphereOf(atomic), false, true, false), atomic);
}

std::vector<graphics::SceneLight> SceneLighting::objectLights(rw::Atomic* atomic) {
    // The selection drawObjectAtomic() makes.
    const graphics::LightSphere sphere = sphereOf(atomic);
    return lightsOf(selectFor(distanceSq(sphere.centre, m_pose.position), sphere, true, true, false), atomic);
}

std::vector<graphics::SceneLight> SceneLighting::carLights(rw::Atomic* atomic) {
    // The selection drawCarAtomic() makes.
    return lightsOf(selectFor(0.0F, sphereOf(atomic), true, true, false), atomic);
}

std::vector<graphics::SceneLight> SceneLighting::lightsOf(const graphics::LightSelection& selection,
                                                          rw::Atomic* atomic) {
    // The selection kept for the lighting frame's row, and the lights the lab's layers keep, by value.
    m_pending = selection;
    const graphics::LightSelection drawn =
        m_switches != nullptr
            ? graphics::layeredSelection(selection, m_lighting.lights, m_switches->layers, m_switches->view)
            : selection;
    std::vector<graphics::SceneLight> lights;
    for (const std::uint16_t index : drawn.lights()) {
        lights.push_back(sceneLightOf(index));
    }
    ++m_atomicsDrawn;
    recordSelection(atomic);
    return lights;
}

void SceneLighting::drawWorldAtomic(rw::Atomic* atomic) {
    useLights(selectFor(0.0F, sphereOf(atomic), false, true, false));
    // The sectors are the only geometry the original draws with mipmaps, levels picked by distance
    // (docs/research/rendering.md#world).
    setDistanceMipmaps(true);
    m_routed = true;
    renderInRig(atomic);
    m_routed = false;
    setDistanceMipmaps(false);
}

void SceneLighting::drawBackgroundAtomic(rw::Atomic* atomic) {
    useLights(selectFor(0.0F, sphereOf(atomic), false, false, false));
    // The background's models carry no light flag (nor normals), so librw would draw them by their prelighting
    // alone; the original adds the world's ambient to them all the same (level99's sky box: prelight 60 + 0.227
    // reaches the GS as 59 of 128; docs/research/lighting.md#coneys-implementation). Lit for the draw: with no normals
    // the directional lights add nothing.
    rw::Geometry* geometry = atomic->geometry;
    const rw::uint32 flags = geometry->flags;
    geometry->flags |= rw::Geometry::LIGHT;
    m_background = true;
    m_routed = true;
    renderInRig(atomic);
    m_routed = false;
    m_background = false;
    geometry->flags = flags;
}

void SceneLighting::drawHumanAtomic(rw::Atomic* atomic, bool hidden) {
    const graphics::LightSphere sphere = sphereOf(atomic);
    useLights(selectFor(distanceSq(sphere.centre, m_pose.position), sphere, true, !hidden, false));
    renderInRig(atomic);
}

void SceneLighting::drawObjectAtomic(rw::Atomic* atomic) {
    const graphics::LightSphere sphere = sphereOf(atomic);
    useLights(selectFor(distanceSq(sphere.centre, m_pose.position), sphere, true, true, false));
    // Through the draw layer, as the world's atomics (item 8 of docs/engine/renderer.md#order): the cars draw here too.
    m_routed = true;
    renderInRig(atomic);
    m_routed = false;
}

void SceneLighting::drawCarAtomic(rw::Atomic* atomic) {
    // Squared distance 0, as CarInstance_Render clears it (0x00172cc8): a car keeps the nearest cap however far it is.
    useLights(selectFor(0.0F, sphereOf(atomic), true, true, false));
    m_routed = true;
    renderInRig(atomic);
    m_routed = false;
}

graphics::LightSelection SceneLighting::selectFor(float distSq, const graphics::LightSphere& sphere, bool objects,
                                                  bool pointLights, bool pulse) {
    m_pendingDistSq = distSq;
    return m_lighting.lights.select(distSq, sphere, objects, pointLights, pulse);
}

void SceneLighting::drawGroundRings(std::span<const hud::GroundRing> rings,
                                    std::span<const hud::TargetMarker> markers) const {
    rw::Texture* texture = rwTextureOf(m_shadowSheet);
    if (texture == nullptr || !m_shadowSheet) {
        return;
    }
    const graphics::ParticlePage& page = m_shadowSheet->page;
    if (static_cast<std::size_t>(hud::kRingRect) >= page.rects.size()) {
        return;
    }
    // Game axes into RenderWare's, (x, y, z) to (x, z, -y).
    const auto toRwPoint = [](anim::Vec3 p) { return rw::V3d{p.x, p.z, -p.y}; };
    std::vector<rw::gl3::Im3DVertex> vertices;
    // The rings: centred on the middle of rectangle 1, each rim vertex offset in whole-sheet units.
    const graphics::UvRect& band = page.rect(static_cast<std::size_t>(hud::kRingRect));
    const float midU = (band.u0 + band.u1) * 0.5F;
    const float midV = (band.v0 + band.v1) * 0.5F;
    for (const hud::GroundRing& ring : rings) {
        for (const hud::RingVertex& v : hud::ringFan(ring)) {
            vertices.push_back(vertex(toRwPoint(v.position), midU + v.du, midV + v.dv, v.rgba));
        }
    }
    // The L1 markers: flat squares of rectangle 0, white (black in Rumble) at the target ring's alpha.
    if (static_cast<std::size_t>(hud::kTargetMarkerRect) < page.rects.size()) {
        const graphics::UvRect& uv = page.rect(static_cast<std::size_t>(hud::kTargetMarkerRect));
        for (const hud::TargetMarker& marker : markers) {
            const float half = marker.size * 0.5F;
            const std::uint8_t shade = marker.black ? 0 : 255;
            addQuad(vertices, toRwPoint(marker.centre), rw::V3d{half, 0.0F, 0.0F}, rw::V3d{0.0F, 0.0F, -half}, uv,
                    {shade, shade, shade, marker.alpha});
        }
    }
    drawQuads(vertices, texture);
}

void SceneLighting::drawCoronas() const {
    rw::Texture* texture = rwTextureOf(m_coronaSheet);
    if (texture == nullptr || !m_coronaSheet) {
        return;
    }
    const graphics::ParticlePage& page = m_coronaSheet->page;
    // Camera-facing squares: spanned by the camera's right and up axes.
    std::vector<rw::gl3::Im3DVertex> vertices;
    for (const graphics::CoronaSprite& sprite : m_lighting.lights.coronas()) {
        if (sprite.rect < 0 || static_cast<std::size_t>(sprite.rect) >= page.rects.size()) {
            continue;
        }
        const float half = sprite.size * 0.5F;
        const rw::V3d x{m_pose.right.x * half, m_pose.right.y * half, m_pose.right.z * half};
        const rw::V3d y{m_pose.up.x * half, m_pose.up.y * half, m_pose.up.z * half};
        addQuad(vertices, toRw(sprite.position), x, y, page.rect(static_cast<std::size_t>(sprite.rect)), sprite.rgba);
    }
    drawQuads(vertices, texture);
}

void SceneLighting::drawBlobShadows(std::span<const graphics::BlobShadow> shadows) const {
    rw::Texture* texture = rwTextureOf(m_shadowSheet);
    if (texture == nullptr || !m_shadowSheet) {
        return;
    }
    const graphics::ParticlePage& page = m_shadowSheet->page;
    if (static_cast<std::size_t>(graphics::kBlobShadowRect) >= page.rects.size()) {
        return;
    }
    const graphics::UvRect& uv = page.rect(graphics::kBlobShadowRect);
    std::vector<rw::gl3::Im3DVertex> vertices;
    for (const graphics::BlobShadow& shadow : shadows) {
        // Game axes (z up) into RenderWare's (x, z, -y); the square lies across the ground's normal.
        const rw::V3d centre{shadow.centre.x, shadow.centre.z, -shadow.centre.y};
        const rw::V3d normal{shadow.normal.x, shadow.normal.z, -shadow.normal.y};
        const rw::V3d axis = std::fabs(normal.x) < 0.9F ? rw::V3d{1.0F, 0.0F, 0.0F} : rw::V3d{0.0F, 0.0F, 1.0F};
        const rw::V3d along = rw::normalize(rw::cross(normal, axis));
        const rw::V3d across = rw::cross(normal, along);
        const float half = shadow.size * 0.5F;
        addQuad(vertices, centre, rw::V3d{along.x * half, along.y * half, along.z * half},
                rw::V3d{across.x * half, across.y * half, across.z * half}, uv, graphics::kBlobShadowColour);
    }
    drawQuads(vertices, texture);
}

void SceneLighting::drawGlass(std::span<const world_objects::GlassQuad> panes) const {
    rw::Texture* texture = rwTextureOf(m_shadowSheet);
    if (texture == nullptr || !m_shadowSheet) {
        return;
    }
    const graphics::ParticlePage& page = m_shadowSheet->page;
    std::vector<rw::gl3::Im3DVertex> vertices;
    for (const world_objects::GlassQuad& pane : panes) {
        if (pane.rect >= page.rects.size()) {
            continue;
        }
        const graphics::UvRect& uv = page.rect(pane.rect);
        const std::array<std::uint8_t, 4> rgba{
            static_cast<std::uint8_t>(pane.colour >> 24U), static_cast<std::uint8_t>(pane.colour >> 16U),
            static_cast<std::uint8_t>(pane.colour >> 8U), static_cast<std::uint8_t>(pane.colour)};
        // Game axes into RenderWare's (x, z, -y); the strip's corners show (u1, v1), (u0, v1), (u1, v0), (u0, v0).
        const auto at = [&pane](std::size_t i) {
            const anim::Vec3& p = pane.corners.at(i);
            return rw::V3d{p.x, p.z, -p.y};
        };
        const rw::gl3::Im3DVertex a = vertex(at(0), uv.u1, uv.v1, rgba);
        const rw::gl3::Im3DVertex b = vertex(at(1), uv.u0, uv.v1, rgba);
        const rw::gl3::Im3DVertex c = vertex(at(2), uv.u1, uv.v0, rgba);
        const rw::gl3::Im3DVertex d = vertex(at(3), uv.u0, uv.v0, rgba);
        vertices.insert(vertices.end(), {a, b, c, c, b, d});
    }
    drawQuads(vertices, texture);
}

} // namespace coney::platform
