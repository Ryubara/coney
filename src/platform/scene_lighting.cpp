// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/scene_lighting.h"

#include <array>
#include <cmath>
#include <utility>

#include <rw.h>

#include "platform/sprite_sheets.h"
#include "platform/world_atomic.h"

namespace coney::platform {

namespace {

// The first passes' alpha test reference, 0x40 of the GS's 0x80, in librw's 0-255.
constexpr rw::uint32 kFirstPassAlphaRef = 128;

// librw's vector from ours.
rw::V3d toRw(world::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

// The librw light type of a record's.
rw::int32 rwTypeOf(graphics::LightType type) {
    switch (type) {
    case graphics::LightType::Point:
        return rw::Light::POINT;
    case graphics::LightType::Spot:
        return rw::Light::SPOT;
    case graphics::LightType::Directional:
        return rw::Light::DIRECTIONAL;
    case graphics::LightType::Ambient:
        return rw::Light::AMBIENT;
    }
    return rw::Light::AMBIENT;
}

// A frame matrix whose `at` axis is `direction` (unit) and whose position is `position`: a directional or spot light
// shines along `at`.
rw::Matrix frameMatrix(world::Vec3 position, world::Vec3 direction) {
    rw::V3d at = toRw(direction);
    const float length = std::sqrt(at.x * at.x + at.y * at.y + at.z * at.z);
    at = length > 0.0F ? rw::V3d{at.x / length, at.y / length, at.z / length} : rw::V3d{0.0F, -1.0F, 0.0F};
    // Any right-handed set with `at`: up from whichever world axis is least along it.
    const rw::V3d axis = std::fabs(at.y) < 0.9F ? rw::V3d{0.0F, 1.0F, 0.0F} : rw::V3d{1.0F, 0.0F, 0.0F};
    const rw::V3d right = rw::normalize(rw::cross(axis, at));
    rw::Matrix matrix;
    matrix.setIdentity();
    matrix.right = right;
    matrix.up = rw::cross(at, right);
    matrix.at = at;
    matrix.pos = toRw(position);
    matrix.update();
    return matrix;
}

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
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 0);
    rw::SetRenderState(rw::FOGENABLE, 0);
    rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
    rw::SetRenderState(rw::VERTEXALPHA, 1);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    rw::SetRenderStatePtr(rw::TEXTURERASTER, texture != nullptr ? texture->raster : nullptr);
    rw::im3d::Transform(vertices.data(), static_cast<rw::int32>(vertices.size()), nullptr,
                        rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA | rw::im3d::VERTEXUV);
    rw::im3d::RenderPrimitive(rw::PRIMTYPETRILIST);
    rw::im3d::End();
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
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
    m_pose = pose;
    m_lighting.lights.beginViewport(
        graphics::LightView{
            .position = pose.position, .forward = pose.forward, .nearClip = nearClip, .farClip = farClip},
        nowMs);
}

rw::Light* SceneLighting::mirror(std::uint16_t index) {
    const graphics::LightRecord& record = m_lighting.lights.record(index);
    rw::Light*& light = m_mirrors.at(index);
    // A record reused with another type needs a light of that type.
    if (light != nullptr && light->getType() != rwTypeOf(record.desc.type)) {
        rw::Frame* frame = light->getFrame();
        light->destroy();
        if (frame != nullptr) {
            frame->destroy();
        }
        light = nullptr;
    }
    if (light == nullptr) {
        light = rw::Light::create(rwTypeOf(record.desc.type));
        light->setFlags(rw::Light::LIGHTATOMICS);
        light->setFrame(rw::Frame::create());
    }
    light->setColor(record.current.r, record.current.g, record.current.b);
    light->radius = record.desc.radius;
    if (record.desc.type == graphics::LightType::Spot) {
        light->setAngle(record.desc.coneAngle);
    }
    rw::Matrix matrix = frameMatrix(record.desc.position, record.desc.direction);
    light->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
    return light;
}

void SceneLighting::useLights(const graphics::LightSelection& selection) {
    for (rw::Light* light : m_inWorld) {
        m_world->removeLight(light);
    }
    m_inWorld.clear();
    for (const std::uint16_t index : selection.lights()) {
        rw::Light* light = mirror(index);
        m_world->addLight(light);
        m_inWorld.push_back(light);
    }
}

void SceneLighting::renderInRig(rw::Atomic* atomic) {
    // librw lights an atomic's point lights only when the atomic is in the current world: put it there for the draw.
    rw::World* current = rw::engine->currentWorld;
    rw::World* home = atomic->world;
    rw::engine->currentWorld = m_world;
    atomic->world = m_world;
    // A first pass (GS context 1, docs/research/rendering.md#shared-state) tests alpha GEQUAL 0x40 of 0x80 with AFAIL
    // FB_ONLY: a fainter pixel is still blended but writes no Z, so what lies behind it (a wall behind a decal's
    // clear part) still draws. Passes that turn the test off (second layers, blood) are drawn as they ask.
    const bool firstPass = rw::GetRenderState(rw::ALPHATESTFUNC) == rw::ALPHAGREATEREQUAL;
    if (firstPass) {
        rw::SetRenderState(rw::GSALPHATEST, 1);
        rw::SetRenderState(rw::GSALPHATESTREF, kFirstPassAlphaRef);
    }
    atomic->render();
    if (firstPass) {
        rw::SetRenderState(rw::GSALPHATEST, 0);
    }
    // A dual material's second layer right after it, on the same vertices (docs/research/rendering.md#dual): blended
    // by its texture's alpha with no alpha test, writing Z, at the base's fade.
    if (rw::Atomic* layer = dualLayerOf(atomic); layer != nullptr) {
        const rw::Geometry* base = atomic->geometry;
        const rw::uint8 fade = base->matList.numMaterials > 0 ? base->matList.materials[0]->color.alpha : 255;
        rw::Geometry* second = layer->geometry;
        for (rw::int32 i = 0; i < second->matList.numMaterials; ++i) {
            second->matList.materials[i]->color.alpha = fade;
        }
        const rw::uint32 testFunc = rw::GetRenderState(rw::ALPHATESTFUNC);
        rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
        layer->world = m_world;
        layer->render();
        layer->world = nullptr;
        rw::SetRenderState(rw::ALPHATESTFUNC, testFunc);
    }
    atomic->world = home;
    rw::engine->currentWorld = current;
}

void SceneLighting::drawWorldAtomic(rw::Atomic* atomic) {
    useLights(m_lighting.lights.select(0.0F, sphereOf(atomic), false, true, false));
    renderInRig(atomic);
}

void SceneLighting::drawBackgroundAtomic(rw::Atomic* atomic) {
    useLights(m_lighting.lights.select(0.0F, sphereOf(atomic), false, false, false));
    renderInRig(atomic);
}

void SceneLighting::drawHumanAtomic(rw::Atomic* atomic, bool hidden) {
    const graphics::LightSphere sphere = sphereOf(atomic);
    useLights(m_lighting.lights.select(distanceSq(sphere.centre, m_pose.position), sphere, true, !hidden, false));
    renderInRig(atomic);
}

void SceneLighting::drawObjectAtomic(rw::Atomic* atomic) {
    const graphics::LightSphere sphere = sphereOf(atomic);
    useLights(m_lighting.lights.select(distanceSq(sphere.centre, m_pose.position), sphere, true, true, false));
    renderInRig(atomic);
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
