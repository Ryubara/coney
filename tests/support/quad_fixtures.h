// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A librw atomic of one quad, made in a test, for the tests that put a mesh through the scene assets and the frame
// description (docs/engine/gpu-renderer.md#scene): the corners, how the geometry is flagged and coloured, and up to two
// materials (the quad's two triangles, one each). Nothing here comes from the game (LEGAL.md, "No game data").

#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>

#include "support/librw_once.h"

#include "graphics/render_device.h"

namespace coney::test {

/// What a test quad is.
struct QuadSpec {
    std::array<float, 3> topLeft{-1.0F, 1.0F, 5.0F};
    std::array<float, 3> topRight{1.0F, 1.0F, 5.0F};
    std::array<float, 3> bottomLeft{-1.0F, -1.0F, 5.0F};
    std::array<float, 3> bottomRight{1.0F, -1.0F, 5.0F};
    bool lit = false;      ///< RenderWare's LIGHT, with normals facing the camera (-z).
    bool modulate = false; ///< MODULATE.
    bool prelit = false;   ///< PRELIT, every vertex `prelight`.
    graphics::Rgba prelight{255, 255, 255, 255};
    graphics::Rgba colour{255, 255, 255, 255}; ///< The first material's colour.
    std::string texture{};                     ///< The first material's texture's name; none for empty.
    /// A second material for the quad's second triangle: its colour and texture name.
    std::optional<graphics::Rgba> secondColour{};
    std::string secondTexture{};
};

/// A librw atomic of one quad as `spec` says: texture coordinates (0, 0) at its top left to (1, 1) at its bottom right,
/// wound counter-clockwise as the camera sees it. The caller destroys it with destroyQuad().
inline rw::Atomic* makeQuad(const QuadSpec& spec) {
    rw::uint32 flags = rw::Geometry::POSITIONS | rw::Geometry::NORMALS | rw::Geometry::TEXTURED;
    if (spec.lit) {
        flags |= rw::Geometry::LIGHT;
    }
    if (spec.modulate) {
        flags |= rw::Geometry::MODULATE;
    }
    if (spec.prelit) {
        flags |= rw::Geometry::PRELIT;
    }
    rw::Geometry* geometry = rw::Geometry::create(4, 2, flags);
    rw::MorphTarget& target = geometry->morphTargets[0];
    const std::array<std::array<float, 3>, 4> corners{spec.topLeft, spec.topRight, spec.bottomLeft, spec.bottomRight};
    const std::array<rw::TexCoords, 4> uvs{{{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}}};
    for (rw::int32 i = 0; i < 4; ++i) {
        const auto& corner = corners[static_cast<std::size_t>(i)];
        target.vertices[i] = rw::makeV3d(corner[0], corner[1], corner[2]);
        target.normals[i] = rw::makeV3d(0.0F, 0.0F, -1.0F);
        geometry->texCoords[0][i] = uvs[static_cast<std::size_t>(i)];
        if (spec.prelit) {
            geometry->colors[i] = rw::makeRGBA(spec.prelight.r, spec.prelight.g, spec.prelight.b, spec.prelight.a);
        }
    }
    // Corners 0 top left, 1 top right, 2 bottom left, 3 bottom right: (0, 2, 1) and (1, 2, 3) wind counter-clockwise on
    // a screen whose y is up, the front faces OpenGL keeps.
    const std::array<std::array<rw::uint16, 3>, 2> front{{{0, 2, 1}, {1, 2, 3}}};
    for (std::size_t t = 0; t < 2; ++t) {
        rw::Triangle& triangle = geometry->triangles[t];
        triangle.v[0] = front[t][0];
        triangle.v[1] = front[t][1];
        triangle.v[2] = front[t][2];
        triangle.matId = (t == 1 && spec.secondColour) ? 1 : 0;
    }
    const auto addMaterial = [geometry](graphics::Rgba colour, const std::string& texture) {
        rw::Material* material = rw::Material::create();
        material->color = rw::makeRGBA(colour.r, colour.g, colour.b, colour.a);
        if (!texture.empty()) {
            rw::Texture* named = rw::Texture::create(nullptr);
            std::memcpy(named->name, texture.c_str(), texture.size() + 1);
            material->setTexture(named);
            named->destroy(); // the material holds its own reference
        }
        geometry->matList.appendMaterial(material);
        material->destroy(); // the list holds its own reference
    };
    addMaterial(spec.colour, spec.texture);
    if (spec.secondColour) {
        addMaterial(*spec.secondColour, spec.secondTexture);
    }
    geometry->buildMeshes();
    geometry->calculateBoundingSphere();
    rw::Atomic* atomic = rw::Atomic::create();
    atomic->setGeometry(geometry, 0);
    geometry->destroy(); // the atomic holds its own reference
    atomic->setFrame(rw::Frame::create());
    return atomic;
}

/// Destroys `atomic` and its frame.
inline void destroyQuad(rw::Atomic* atomic) {
    rw::Frame* frame = atomic->getFrame();
    atomic->setFrame(nullptr);
    atomic->destroy();
    frame->destroy();
}

} // namespace coney::test
