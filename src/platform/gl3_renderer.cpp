// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gl3_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include <rw.h>

#include "core/assert.h"
#include "platform/character_mesh.h"
#include "platform/character_skin.h"
#include "platform/draw_calls.h"
#include "platform/movie_screen.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_lod.h"

namespace coney::platform {

namespace {

// librw's Camera::create starts a camera's near plane at 0.05: the 2D vertices' depth while the NULL backend records.
constexpr float kNewCameraNearPlane = 0.05F;

// Whether 2D draws are made: with a camera to draw through, or while the draw layer records.
bool drawing2d(const Gl3Renderer::Target& target) { return target.camera != nullptr || draw::recording(); }

// librw's addressing for `address` (the 2D side records only Clamp and Wrap; the rest map one to one).
rw::Texture::Addressing rwAddress(graphics::TextureAddress address) {
    switch (address) {
    case graphics::TextureAddress::Wrap:
        return rw::Texture::WRAP;
    case graphics::TextureAddress::Mirror:
        return rw::Texture::MIRROR;
    case graphics::TextureAddress::Clamp:
        return rw::Texture::CLAMP;
    case graphics::TextureAddress::Border:
        return rw::Texture::BORDER;
    }
    return rw::Texture::CLAMP;
}

// librw's face culling for `faces`.
rw::uint32 rwCull(graphics::CullFaces faces) {
    switch (faces) {
    case graphics::CullFaces::None:
        return rw::CULLNONE;
    case graphics::CullFaces::Front:
        return rw::CULLFRONT;
    case graphics::CullFaces::Back:
        break;
    }
    return rw::CULLBACK;
}

// librw's filter for `filter`.
rw::Texture::FilterMode rwFilter(graphics::TextureFilter filter) {
    switch (filter) {
    case graphics::TextureFilter::Nearest:
        return rw::Texture::NEAREST;
    case graphics::TextureFilter::Linear:
        break;
    case graphics::TextureFilter::MipNearest:
        return rw::Texture::MIPNEAREST;
    case graphics::TextureFilter::MipLinear:
        return rw::Texture::MIPLINEAR;
    case graphics::TextureFilter::LinearMipNearest:
        return rw::Texture::LINEARMIPNEAREST;
    case graphics::TextureFilter::LinearMipLinear:
        return rw::Texture::LINEARMIPLINEAR;
    }
    return rw::Texture::LINEAR;
}

// The librw texture of a sprite sheet's texture, or a movie's frame (platform/movie_screen.h); null for none.
rw::Texture* rwTextureOf(const graphics::Texture* texture) {
    if (texture == nullptr) {
        return nullptr;
    }
    rw::Texture* rwTexture = nullptr;
    if (const auto* sheetTexture = dynamic_cast<const SheetTexture*>(texture); sheetTexture != nullptr) {
        rwTexture = sheetTexture->rwTexture();
    } else if (const auto* movieTexture = dynamic_cast<const MovieTexture*>(texture); movieTexture != nullptr) {
        rwTexture = movieTexture->rwTexture();
    }
    CONEY_ASSERT(rwTexture != nullptr);
    return rwTexture;
}

} // namespace

// Drops `raster` if there is one (shared with gl3_screen_passes.cpp).
void Gl3Renderer::destroyRaster(rw::Raster*& raster) {
    if (raster != nullptr) {
        raster->destroy();
        raster = nullptr;
    }
}

// The near plane 2D vertices take their depth from (shared with gl3_screen_passes.cpp).
float Gl3Renderer::nearPlaneOf(const Target& target) {
    return target.camera != nullptr ? target.camera->nearPlane : kNewCameraNearPlane;
}

Gl3Renderer::~Gl3Renderer() { releaseAll(); }

void Gl3Renderer::beginFrame() {
    m_pass = 0;
    m_draw = 0;
    m_passStarted = false;
}

void Gl3Renderer::drawNew(const graphics::FrameDescription& frame, const Target& target) {
    const std::span<const graphics::FramePass> passes = frame.passes();
    while (m_pass < passes.size()) {
        const graphics::FramePass& pass = passes[m_pass];
        if (const auto* overlay = std::get_if<graphics::Overlay2dPass>(&pass); overlay != nullptr) {
            for (; m_draw < overlay->draws.size(); ++m_draw) {
                drawOverlay(frame, overlay->draws[m_draw], target);
            }
            // The last pass may still grow: its next draws are drawn by the next call.
            if (m_pass + 1 == passes.size()) {
                return;
            }
        } else if (const auto* scene = std::get_if<graphics::ScenePass>(&pass); scene != nullptr) {
            // A pass that clears the depth first does it once, as it starts, before any of its draws.
            if (scene->clearDepth && !m_passStarted) {
                clearDepth(frame, target);
            }
            m_passStarted = true;
            for (; m_draw < scene->draws.size(); ++m_draw) {
                drawScene(frame, scene->draws[m_draw], target);
            }
            if (m_pass + 1 == passes.size()) {
                return;
            }
        } else if (const auto* effects = std::get_if<graphics::EffectPass>(&pass); effects != nullptr) {
            for (; m_draw < effects->draws.size(); ++m_draw) {
                drawEffect(frame, effects->draws[m_draw], target);
            }
            if (m_pass + 1 == passes.size()) {
                return;
            }
        } else {
            drawScreenPass(std::get<graphics::ScreenPass>(pass), target);
        }
        ++m_pass;
        m_draw = 0;
        m_passStarted = false;
    }
}

void Gl3Renderer::clearDepth(const graphics::FrameDescription& frame, const Target& target) {
    if (target.camera == nullptr && !draw::recording()) {
        return;
    }
    // Z alone: librw takes a colour all the same, the frame's clear (the fog's, for a level).
    const graphics::Rgba clear = frame.start().clear;
    rw::RGBA colour = rw::makeRGBA(clear.r, clear.g, clear.b, clear.a);
    draw::clearCamera(target.camera, &colour, rw::Camera::CLEARZ);
}

graphics::ScreenCopyId Gl3Renderer::newScreenCopy() {
    const graphics::ScreenCopyId id = m_nextCopy++;
    m_kept.push_back(KeptCopy{id});
    return id;
}

void Gl3Renderer::releaseScreenCopy(graphics::ScreenCopyId id) {
    const auto found = std::find_if(m_kept.begin(), m_kept.end(), [id](const KeptCopy& copy) { return copy.id == id; });
    if (found != m_kept.end()) {
        destroyRaster(found->raster);
        m_kept.erase(found);
    }
}

void Gl3Renderer::releaseViewCopies() {
    destroyRaster(m_lineRaster);
    destroyRaster(m_blurRaster);
    destroyRaster(m_halfRaster);
}

void Gl3Renderer::releaseAll() {
    releaseViewCopies();
    m_lights.reset();
    // The numbers stay given out: a pass that still holds one finds its copy gone and makes it again.
    for (KeptCopy& copy : m_kept) {
        destroyRaster(copy.raster);
        copy.kept = false;
    }
}

void Gl3Renderer::drawScene(const graphics::FrameDescription& frame, const graphics::Draw3d& draw,
                            const Target& target) {
    // The mesh views' own passes are the lighting rig's on this renderer (docs/engine/gpu-renderer.md#mesh-views): a
    // draw recorded for the GPU renderer's views is not drawn here.
    if (draw.view != graphics::ViewPass::Shaded) {
        return;
    }
    if ((target.camera == nullptr && !draw::recording()) || target.assets == nullptr) {
        // Nothing to draw into (the NULL or GPU backend): the draw's states still reach the draw layer, so an effect
        // draw after it reads the states GL3 would leave (EffectDraw::inForce).
        applySceneStates(draw.states);
        return;
    }
    rw::Atomic* atomic = target.assets->atomic(draw.mesh);
    if (atomic == nullptr) {
        return;
    }
    applySceneStates(draw.states);
    // The materials' colours as the draw changes them: a sector's fade kept, an object's tint put back after it, a car
    // part's paint kept.
    const MaterialChange colours(*atomic, draw.material);
    // The draw's lights, then the atomic and its dual layer in them, with their texture levels by distance or not
    // (the sectors are the only geometry the original draws with mipmaps, docs/research/rendering.md#world).
    if (!m_lights) {
        m_lights = std::make_unique<LightRig>();
    }
    m_lights->use(frame.lightsOf(draw));
    setDistanceMipmaps(draw.material.distanceLevels);
    if (draw.dualPass == graphics::DualPass::Blood) {
        drawHuman(frame, draw, *atomic, target.assets->atomic(draw.dualLayer));
    } else {
        drawLitAtomic(atomic, target.assets->atomic(draw.dualLayer), m_lights->world(), true);
    }
    setDistanceMipmaps(false);
}

void Gl3Renderer::drawHuman(const graphics::FrameDescription& frame, const graphics::Draw3d& draw, rw::Atomic& atomic,
                            rw::Atomic* blood) {
    // The pose: on the skinned pipeline the shader blends by the palette, filled here from the draw's bones and lent
    // to both atomics for the draw; skinned on the CPU, the mesh blended its vertices from the same matrices when it
    // was posed (CharacterMesh::pose()), so the geometry already holds them.
    const BonePalette* own = nullptr;
    const BonePalette* bloodOwn = nullptr;
    if (characterSkinPalette(&atomic) != nullptr) {
        if (!m_palette) {
            m_palette = std::make_unique<BonePalette>();
        }
        // Zeros past the draw's bones, as a mesh's own palette is before its first pose, so no matrix of the human
        // drawn before is left in it.
        m_palette->fill(0.0F);
        std::vector<anim::Mat34> skinning;
        for (const graphics::Transform3d& bone : frame.bonesOf(draw)) {
            skinning.push_back(anim::Mat34{.x = {bone.right[0], bone.right[1], bone.right[2]},
                                           .y = {bone.up[0], bone.up[1], bone.up[2]},
                                           .z = {bone.at[0], bone.at[1], bone.at[2]},
                                           .t = {bone.position[0], bone.position[1], bone.position[2]}});
        }
        fillBonePalette(skinning, *m_palette);
        own = swapCharacterSkinPalette(&atomic, m_palette.get());
        if (blood != nullptr && characterSkinPalette(blood) != nullptr) {
            bloodOwn = swapCharacterSkinPalette(blood, m_palette.get());
        }
    }
    drawLitAtomic(&atomic, nullptr, m_lights->world(), true);
    if (blood != nullptr) {
        // The blood pass (GS context 2, docs/research/rendering.md#characters): ALPHA 0x44, source over by the blood
        // texture's alpha, with the alpha test off, the same Z test with Z write (equal depths pass librw's LEQUAL),
        // in the body's colours; then the first passes' alpha test again and the vertex alpha off.
        const MaterialChange colours(*blood, draw.material);
        draw::setRenderState(rw::VERTEXALPHA, 1);
        draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
        drawLitAtomic(blood, nullptr, m_lights->world(), true);
        draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
        draw::setRenderState(rw::VERTEXALPHA, 0);
    }
    if (own != nullptr) {
        swapCharacterSkinPalette(&atomic, own);
    }
    if (bloodOwn != nullptr) {
        swapCharacterSkinPalette(blood, bloodOwn);
    }
}

Gl3Renderer::KeptCopy* Gl3Renderer::keptCopy(graphics::ScreenCopyId id) {
    const auto found = std::find_if(m_kept.begin(), m_kept.end(), [id](const KeptCopy& copy) { return copy.id == id; });
    return found != m_kept.end() ? &*found : nullptr;
}

rw::Raster* Gl3Renderer::bindTexture(const graphics::Texture* texture, graphics::TextureAddress address) {
    rw::Texture* rwTexture = rwTextureOf(texture);
    if (rwTexture == nullptr) {
        return nullptr;
    }
    // The texture's own filtering (most of the game's textures ask for linear), clamped at the edges so a rectangle
    // that reaches the texture's border does not pick up texels from the opposite side, unless the draw repeats the
    // texture.
    draw::setRenderState(rw::TEXTUREFILTER, rwTexture->getFilter());
    draw::setRenderState(rw::TEXTUREADDRESS, rwAddress(address));
    return rwTexture->raster;
}

void Gl3Renderer::drawEffect(const graphics::FrameDescription& frame, const graphics::EffectDraw& draw,
                             const Target& target) {
    // NULL or GPU backend, not recording: nothing is drawn, but the states still reach the draw layer, so the draws
    // after it read what GL3 would leave (the effect draws' EffectDraw::inForce, the scene draws' states in force).
    const bool drawing = drawing2d(target);
    const graphics::EffectStates& states = draw.states;
    // The fog as it stands, for the draw that puts it back.
    const bool fogged = draw::getRenderState(rw::FOGENABLE) != 0;
    // Each state the draw sets, set as it always was whatever it already holds (a state set to its own value changes
    // no draw-stream hash, a state never set does).
    if (states.fog) {
        draw::setRenderState(rw::FOGENABLE, *states.fog ? 1 : 0);
    }
    if (states.depthTest) {
        draw::setRenderState(rw::ZTESTENABLE, *states.depthTest ? 1 : 0);
    }
    if (states.depthWrite) {
        draw::setRenderState(rw::ZWRITEENABLE, *states.depthWrite ? 1 : 0);
    }
    if (states.vertexAlpha) {
        draw::setRenderState(rw::VERTEXALPHA, *states.vertexAlpha ? 1 : 0);
    }
    if (states.cull) {
        draw::setRenderState(rw::CULLMODE, rwCull(*states.cull));
    }
    if (states.filter) {
        draw::setRenderState(rw::TEXTUREFILTER, rwFilter(*states.filter));
    }
    if (states.address) {
        draw::setRenderState(rw::TEXTUREADDRESS, rwAddress(*states.address));
    }
    // The texture's raster: none for flat colour.
    if (drawing) {
        const rw::Texture* rwTexture = rwTextureOf(frame.textureOf(draw.texture));
        draw::setRenderStatePtr(rw::TEXTURERASTER, rwTexture != nullptr ? rwTexture->raster : nullptr);
    }
    if (states.blend) {
        draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        draw::setRenderState(rw::DESTBLEND,
                             *states.blend == graphics::EffectBlend::Additive ? rw::BLENDONE : rw::BLENDINVSRCALPHA);
    }

    if (drawing) {
        drawEffectPrimitives(frame, draw);
    }

    // What the draw puts back for the solid draws after it.
    const graphics::EffectRestore& restore = draw.restore;
    if (restore.fog) {
        draw::setRenderState(rw::FOGENABLE, fogged ? 1 : 0);
    }
    if (restore.blend) {
        draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    }
    if (restore.depthWrite) {
        draw::setRenderState(rw::ZWRITEENABLE, 1);
    }
    if (restore.cull) {
        draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
    }
}

void Gl3Renderer::drawEffectPrimitives(const graphics::FrameDescription& frame, const graphics::EffectDraw& draw) {
    // The corners in librw's vertex type, with the colour and texture coordinate a lit sprite has and no normal.
    const std::span<const graphics::EffectVertex> corners = frame.effectVerticesOf(draw);
    std::vector<rw::gl3::Im3DVertex> vertices(corners.size());
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const graphics::EffectVertex& in = corners[i];
        rw::gl3::Im3DVertex& vertex = vertices[i];
        vertex.setX(in.x);
        vertex.setY(in.y);
        vertex.setZ(in.z);
        vertex.setColor(in.colour.r, in.colour.g, in.colour.b, in.colour.a);
        vertex.setU(in.u);
        vertex.setV(in.v);
    }
    const bool lines = draw.shape == graphics::EffectShape::Lines;
    const std::uint32_t flags = lines ? rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA
                                      : rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA | rw::im3d::VERTEXUV;
    draw::im3dTransform(vertices.data(), static_cast<rw::int32>(vertices.size()), nullptr, flags);
    const int primitive = lines ? rw::PRIMTYPELINELIST : rw::PRIMTYPETRILIST;
    if (draw.indexCount == 0) {
        draw::im3dRenderPrimitive(primitive);
    } else {
        // librw takes a mutable pointer.
        std::vector<std::uint16_t> indices(frame.effectIndicesOf(draw).begin(), frame.effectIndicesOf(draw).end());
        draw::im3dRenderIndexedPrimitive(primitive, indices.data(), static_cast<rw::int32>(indices.size()));
    }
    draw::im3dEnd();
}

void Gl3Renderer::set2dStates(rw::Raster* raster, bool opaque) {
    // The 2D pass's states (docs/research/graphics.md#2d-drawing): no depth test or write, no culling, no fog, blended
    // by the vertex and texture alpha over what is already drawn.
    draw::setRenderState(rw::ZTESTENABLE, 0);
    draw::setRenderState(rw::ZWRITEENABLE, 0);
    draw::setRenderState(rw::CULLMODE, rw::CULLNONE);
    draw::setRenderState(rw::FOGENABLE, 0);
    draw::setRenderState(rw::VERTEXALPHA, 1);
    draw::setRenderState(rw::SRCBLEND, opaque ? rw::BLENDONE : rw::BLENDSRCALPHA);
    draw::setRenderState(rw::DESTBLEND, opaque ? rw::BLENDZERO : rw::BLENDINVSRCALPHA);
    draw::setRenderStatePtr(rw::TEXTURERASTER, raster);
}

void Gl3Renderer::drawOverlay(const graphics::FrameDescription& frame, const graphics::Draw2d& draw,
                              const Target& target) {
    if (!drawing2d(target)) {
        return; // NULL backend, not recording: nothing to draw
    }
    rw::Raster* raster = bindTexture(frame.textureOf(draw.material.texture), draw.material.address);
    if (draw.shape == graphics::Shape2d::Quads) {
        drawWindowQuads(raster, frame.quadsOf(draw), target);
        return;
    }

    // Triangles: the 2D states, and the alpha test as the GS's (keep a fragment whose alpha is at least the
    // reference), put back after the draw.
    set2dStates(raster, false);
    const rw::uint32 testFunction = draw::getRenderState(rw::ALPHATESTFUNC);
    const rw::uint32 testRef = draw::getRenderState(rw::ALPHATESTREF);
    if (draw.material.alphaRef) {
        if (*draw.material.alphaRef > 0.0F) {
            draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
            draw::setRenderState(rw::ALPHATESTREF,
                                 static_cast<rw::uint32>(std::lround(*draw.material.alphaRef * 255.0F)));
        } else {
            draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS); // every fragment kept, however faint
        }
    }
    const std::span<const graphics::ScreenVertex> corners = frame.verticesOf(draw);
    const float nearZ = draw::im2dNearZ();
    const float recipZ = 1.0F / nearPlaneOf(target);
    std::vector<rw::gl3::Im2DVertex> vertices(corners.size());
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const graphics::ScreenVertex& in = corners[i];
        rw::gl3::Im2DVertex& vertex = vertices[i];
        vertex.setScreenX(in.x);
        vertex.setScreenY(in.y);
        vertex.setScreenZ(nearZ);
        vertex.setRecipCameraZ(recipZ);
        vertex.setColor(in.colour.r, in.colour.g, in.colour.b, in.colour.a);
        vertex.setU(in.u, recipZ);
        vertex.setV(in.v, recipZ);
    }
    // In runs of whole triangles that fit librw's 2D vertex buffer (draw::kIm2dMaxVertices): a longer call raised
    // GL_INVALID_VALUE and drew nothing right.
    constexpr std::size_t kMostVertices = (static_cast<std::size_t>(draw::kIm2dMaxVertices) / 3) * 3;
    for (std::size_t first = 0; first < vertices.size(); first += kMostVertices) {
        const std::size_t count = std::min(kMostVertices, vertices.size() - first);
        draw::im2dRenderPrimitive(rw::PRIMTYPETRILIST, &vertices[first], static_cast<rw::int32>(count));
    }
    draw::setRenderState(rw::ALPHATESTFUNC, testFunction);
    draw::setRenderState(rw::ALPHATESTREF, testRef);
}

void Gl3Renderer::drawWindowQuads(rw::Raster* raster, std::span<const graphics::LogicalQuad> quads,
                                  const Target& target, bool opaque) {
    set2dStates(raster, opaque);

    // Four corners and two triangles per quad, clockwise from the top left, as the device's screen quads.
    const float nearZ = draw::im2dNearZ();
    const float recipZ = 1.0F / nearPlaneOf(target);
    std::vector<rw::gl3::Im2DVertex> vertices(quads.size() * 4);
    for (std::size_t q = 0; q < quads.size(); ++q) {
        const graphics::LogicalQuad& quad = quads[q];
        struct Corner {
            float x, y, u, v;
        };
        const std::array<Corner, 4> corners{{{quad.x, quad.y, quad.uv.u0, quad.uv.v0},
                                             {quad.x + quad.width, quad.y, quad.uv.u1, quad.uv.v0},
                                             {quad.x + quad.width, quad.y + quad.height, quad.uv.u1, quad.uv.v1},
                                             {quad.x, quad.y + quad.height, quad.uv.u0, quad.uv.v1}}};
        for (std::size_t i = 0; i < corners.size(); ++i) {
            rw::gl3::Im2DVertex& vertex = vertices[(q * 4) + i];
            vertex.setScreenX(corners[i].x);
            vertex.setScreenY(corners[i].y);
            vertex.setScreenZ(nearZ);
            vertex.setRecipCameraZ(recipZ);
            vertex.setColor(quad.colour.r, quad.colour.g, quad.colour.b, quad.colour.a);
            vertex.setU(corners[i].u, recipZ);
            vertex.setV(corners[i].v, recipZ);
        }
    }
    // Draw in runs, each indexing its own vertices from 0 (so 16-bit indices reach), of the most quads librw's 2D
    // buffers take in one call (draw::kIm2dMaxVertices): a longer call raised GL_INVALID_VALUE and drew nothing right,
    // as the debug menu's text does with Coney's bitmap font, one quad a lit pixel.
    constexpr auto kMaxQuadsPerDraw =
        static_cast<std::size_t>(std::min(draw::kIm2dMaxVertices / 4, draw::kIm2dMaxIndices / 6));
    std::vector<std::uint16_t> indices;
    for (std::size_t first = 0; first < quads.size(); first += kMaxQuadsPerDraw) {
        const std::size_t count = std::min(kMaxQuadsPerDraw, quads.size() - first);
        indices.clear();
        indices.reserve(count * 6);
        for (std::size_t q = 0; q < count; ++q) {
            for (const std::size_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
                indices.push_back(static_cast<std::uint16_t>((q * 4) + corner));
            }
        }
        draw::im2dRenderIndexedPrimitive(rw::PRIMTYPETRILIST, &vertices[first * 4], static_cast<rw::int32>(count * 4),
                                         indices.data(), static_cast<rw::int32>(indices.size()));
    }
}

bool Gl3Renderer::copyToRaster(rw::Raster*& raster, int x, int y, int width, int height, const Target& target) {
    if (raster != nullptr && (raster->width != width || raster->height != height)) {
        destroyRaster(raster);
    }
    if (raster == nullptr) {
        raster = draw::createRaster(width, height, 32, static_cast<rw::int32>(rw::Raster::TEXTURE) | rw::Raster::C8888);
        if (raster == nullptr) {
            return false;
        }
    }
    // GL counts the frame buffer's rows from the bottom. librw's 2D drawing samples the copy with v = 0 at the
    // rectangle's top (measured: drawn that way, the copy shows the screen the right way up).
    draw::copyFrameToRaster(raster, x, y, width, height, target.frameSize.height);
    return true;
}

} // namespace coney::platform
