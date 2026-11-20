// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/lit_atomic.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <rw.h>

#include "platform/draw_calls.h"

namespace coney::platform {

namespace {

// librw's vector from three floats.
rw::V3d toRw(const std::array<float, 3>& v) { return rw::V3d{v[0], v[1], v[2]}; }

// A frame matrix whose `at` axis is `direction` (made unit) and whose position is `position`: a directional or spot
// light shines along `at`.
rw::Matrix lightMatrix(const std::array<float, 3>& position, const std::array<float, 3>& direction) {
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

// Destroys `light` and its frame, which Light::destroy leaves alone.
void destroyLight(rw::Light* light) {
    rw::Frame* frame = light->getFrame();
    light->destroy();
    if (frame != nullptr) {
        frame->destroy();
    }
}

// Sets render state `state` to `value` only when it differs from the one in force: a 3D draw's states are those the
// pass set before it, so nothing is set again and the draw stream sees what it saw before
// (docs/engine/renderer.md#comparing).
void setIfChanged(rw::int32 state, rw::uint32 value) {
    if (draw::getRenderState(state) != value) {
        draw::setRenderState(state, value);
    }
}

// librw's cull mode for `faces`.
rw::uint32 rwCull(graphics::CullFaces faces) {
    switch (faces) {
    case graphics::CullFaces::None:
        return rw::CULLNONE;
    case graphics::CullFaces::Back:
        return rw::CULLBACK;
    case graphics::CullFaces::Front:
        return rw::CULLFRONT;
    }
    return rw::CULLBACK;
}

// librw's alpha test function for `test`.
rw::uint32 rwAlphaTest(graphics::AlphaTest test) {
    switch (test) {
    case graphics::AlphaTest::Always:
        return rw::ALPHAALWAYS;
    case graphics::AlphaTest::GreaterEqual:
        return rw::ALPHAGREATEREQUAL;
    case graphics::AlphaTest::Less:
        return rw::ALPHALESS;
    }
    return rw::ALPHAALWAYS;
}

} // namespace

std::int32_t rwLightType(graphics::LightType type) {
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

void placeLight(rw::Light& light, const graphics::SceneLight& values) {
    light.setColor(values.colour[0], values.colour[1], values.colour[2]);
    light.radius = values.radius;
    if (values.type == graphics::LightType::Spot) {
        light.setAngle(values.coneAngle);
    }
    rw::Matrix matrix = lightMatrix(values.position, values.direction);
    light.getFrame()->transform(&matrix, rw::COMBINEREPLACE);
}

void drawLitAtomic(rw::Atomic* atomic, rw::Atomic* layer, rw::World* lights, bool routed) {
    // The draw: through the draw layer for the routed atomics, else on librw directly.
    const auto render = [routed](rw::Atomic* drawn) {
        if (routed) {
            draw::renderAtomic(drawn);
        } else {
            drawn->render();
        }
    };
    // librw lights an atomic's point lights only when the atomic is in the current world: put it there for the draw.
    rw::World* current = rw::engine->currentWorld;
    rw::World* home = atomic->world;
    rw::engine->currentWorld = lights;
    atomic->world = lights;
    // A first pass (GS context 1, docs/research/rendering.md#shared-state) tests alpha GEQUAL 0x40 of 0x80 with AFAIL
    // FB_ONLY: a fainter pixel is still blended but writes no Z, so what lies behind it (a wall behind a decal's
    // clear part) still draws. Passes that turn the test off (second layers, blood) are drawn as they ask.
    const bool firstPass = draw::getRenderState(rw::ALPHATESTFUNC) == rw::ALPHAGREATEREQUAL;
    if (firstPass) {
        draw::setRenderState(rw::GSALPHATEST, 1);
        draw::setRenderState(rw::GSALPHATESTREF, kFirstPassAlphaRef);
    }
    render(atomic);
    if (firstPass) {
        draw::setRenderState(rw::GSALPHATEST, 0);
    }
    // A dual material's second layer right after it, on the same vertices (docs/research/rendering.md#dual): blended
    // by its texture's alpha with no alpha test, writing Z, at the base's fade.
    if (layer != nullptr) {
        const rw::Geometry* base = atomic->geometry;
        const rw::uint8 fade = base->matList.numMaterials > 0 ? base->matList.materials[0]->color.alpha : 255;
        rw::Geometry* second = layer->geometry;
        for (rw::int32 i = 0; i < second->matList.numMaterials; ++i) {
            second->matList.materials[i]->color.alpha = fade;
        }
        const rw::uint32 testFunc = draw::getRenderState(rw::ALPHATESTFUNC);
        draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
        layer->world = lights;
        render(layer);
        layer->world = nullptr;
        draw::setRenderState(rw::ALPHATESTFUNC, testFunc);
    }
    atomic->world = home;
    rw::engine->currentWorld = current;
}

LightRig::LightRig() : m_world(rw::World::create()) {}

// Frees the rig's lights, their frames and its private world.
LightRig::~LightRig() {
    // Lights out of the world first, then each light and its frame.
    for (std::size_t i = 0; i < m_inWorld; ++i) {
        m_world->removeLight(m_lights[i]);
    }
    for (rw::Light* light : m_lights) {
        destroyLight(light);
    }
    m_world->destroy();
}

void LightRig::use(std::span<const graphics::SceneLight> lights) {
    for (std::size_t i = 0; i < m_inWorld; ++i) {
        m_world->removeLight(m_lights[i]);
    }
    m_inWorld = 0;
    for (const graphics::SceneLight& values : lights) {
        // The light in this place, made again when it is another type.
        const std::int32_t type = rwLightType(values.type);
        if (m_inWorld == m_lights.size()) {
            m_lights.push_back(nullptr);
        }
        rw::Light*& light = m_lights[m_inWorld];
        if (light != nullptr && light->getType() != type) {
            destroyLight(light);
            light = nullptr;
        }
        if (light == nullptr) {
            light = rw::Light::create(type);
            light->setFlags(rw::Light::LIGHTATOMICS);
            light->setFrame(rw::Frame::create());
        }
        placeLight(*light, values);
        m_world->addLight(light);
        ++m_inWorld;
    }
}

void applySceneStates(const graphics::SceneStates& states) {
    setIfChanged(rw::ZTESTENABLE, states.depthTest ? 1 : 0);
    setIfChanged(rw::ZWRITEENABLE, states.depthWrite ? 1 : 0);
    setIfChanged(rw::CULLMODE, rwCull(states.cull));
    setIfChanged(rw::FOGENABLE, states.fog ? 1 : 0);
    // librw packs the fog colour red in the low byte.
    const graphics::Rgba fog = states.fogColour;
    setIfChanged(rw::FOGCOLOR, static_cast<rw::uint32>(fog.r) | static_cast<rw::uint32>(fog.g) << 8U |
                                   static_cast<rw::uint32>(fog.b) << 16U | static_cast<rw::uint32>(fog.a) << 24U);
    setIfChanged(rw::SRCBLEND, states.alphaBlend ? rw::BLENDSRCALPHA : rw::BLENDONE);
    setIfChanged(rw::DESTBLEND, states.alphaBlend ? rw::BLENDINVSRCALPHA : rw::BLENDZERO);
    setIfChanged(rw::ALPHATESTFUNC, rwAlphaTest(states.alphaTest));
}

graphics::SceneStates sceneStatesInForce() {
    graphics::SceneStates states;
    states.depthTest = draw::getRenderState(rw::ZTESTENABLE) != 0;
    states.depthWrite = draw::getRenderState(rw::ZWRITEENABLE) != 0;
    switch (draw::getRenderState(rw::CULLMODE)) {
    case rw::CULLNONE:
        states.cull = graphics::CullFaces::None;
        break;
    case rw::CULLFRONT:
        states.cull = graphics::CullFaces::Front;
        break;
    default:
        states.cull = graphics::CullFaces::Back;
        break;
    }
    states.fog = draw::getRenderState(rw::FOGENABLE) != 0;
    // librw packs the fog colour red in the low byte.
    const std::uint32_t fog = draw::getRenderState(rw::FOGCOLOR);
    states.fogColour = graphics::Rgba{static_cast<std::uint8_t>(fog & 0xFFU), static_cast<std::uint8_t>(fog >> 8U),
                                      static_cast<std::uint8_t>(fog >> 16U), static_cast<std::uint8_t>(fog >> 24U)};
    states.alphaBlend = draw::getRenderState(rw::SRCBLEND) == rw::BLENDSRCALPHA;
    switch (draw::getRenderState(rw::ALPHATESTFUNC)) {
    case rw::ALPHAGREATEREQUAL:
        states.alphaTest = graphics::AlphaTest::GreaterEqual;
        break;
    case rw::ALPHALESS:
        states.alphaTest = graphics::AlphaTest::Less;
        break;
    default:
        states.alphaTest = graphics::AlphaTest::Always;
        break;
    }
    return states;
}

MaterialChange::MaterialChange(rw::Atomic& atomic, const graphics::Material3d& material) : m_geometry(atomic.geometry) {
    rw::Geometry* geometry = m_geometry;
    switch (material.colour) {
    case graphics::MaterialColour::Fade:
        // Written only where it differs, as the original does.
        for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
            rw::Material* changed = geometry->matList.materials[i];
            if (changed->color.alpha != material.fade) {
                changed->color.alpha = material.fade;
            }
        }
        break;
    case graphics::MaterialColour::Tint: {
        // The tint becomes the first material's colour, replacing it, and the others stay as they are, as
        // ObjectRender_Draw writes it (docs/research/objects.md#tint). The materials are shared by every object of a
        // type, so the first one's own colour is kept to put back (the original leaves it: every object's draw writes
        // it again, so nothing else sees it).
        const graphics::Rgba tint = material.tint;
        if (geometry->matList.numMaterials > 0) {
            rw::RGBA& colour = geometry->matList.materials[0]->color;
            m_own.push_back({colour.red, colour.green, colour.blue, colour.alpha});
            colour = rw::RGBA{tint.r, tint.g, tint.b, tint.a};
        }
        // librw multiplies by the material's colour only for a geometry that modulates: set for the draw when the
        // colour changes anything.
        m_flags = geometry->flags;
        m_restore = true;
        if (tint != graphics::kWhite) {
            geometry->flags |= rw::Geometry::MODULATE;
        }
        break;
    }
    case graphics::MaterialColour::Paint: {
        const rw::RGBA colour{material.tint.r, material.tint.g, material.tint.b, material.tint.a};
        for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
            geometry->matList.materials[i]->color = colour;
        }
        break;
    }
    case graphics::MaterialColour::Shade:
        break;
    }
}

// Puts back what a tint changed: the geometry's flags and the first material's own colour.
MaterialChange::~MaterialChange() {
    if (!m_restore) {
        return;
    }
    m_geometry->flags = m_flags;
    for (std::size_t i = 0; i < m_own.size(); ++i) {
        const std::array<std::uint8_t, 4>& own = m_own[i];
        m_geometry->matList.materials[i]->color = rw::RGBA{own[0], own[1], own[2], own[3]};
    }
}

} // namespace coney::platform
