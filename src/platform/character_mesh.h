// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "animation/anim_math.h"
#include "characters/character_model.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Texture;
} // namespace rw

namespace coney::platform {

/// A character model as plain librw geometry, skinned on the CPU: the model's vertices and triangles in one librw
/// atomic with one material per model material, textured with the character's texture. Each frame the skinned
/// positions and normals (characters::skinVertices()) are written in and librw uploads them again. Owns the atomic, its
/// geometry and frame, and a reference to the texture. Needs a running RenderEngine (either backend) and must be
/// destroyed before it stops. Not copyable or movable: librw keeps pointers to it.
///
/// Research: docs/research/characters.md#coneys-implementation
class CharacterMesh {
  public:
    /// Builds the geometry of `model` in its unskinned (bind) positions. `texture` (may be null: untextured) is used by
    /// every material, as the models' materials name no texture of their own (characters.md); the mesh keeps a
    /// reference to it. Lit by the world's lights with the vertex normals, modulated by the material colour.
    CharacterMesh(const characters::CharacterModel& model, rw::Texture* texture);
    ~CharacterMesh();
    CharacterMesh(const CharacterMesh&) = delete;
    CharacterMesh& operator=(const CharacterMesh&) = delete;
    CharacterMesh(CharacterMesh&&) = delete;
    CharacterMesh& operator=(CharacterMesh&&) = delete;

    /// Replaces every vertex's position and normal; both spans hold one entry per model vertex (checked by
    /// CONEY_ASSERT).
    void update(std::span<const anim::Vec3> positions, std::span<const anim::Vec3> normals);

    /// The librw atomic, for drawing. Valid as long as this object.
    [[nodiscard]] rw::Atomic* atomic() const { return m_atomic; }

  private:
    rw::Atomic* m_atomic = nullptr;   // owned, with its geometry and frame
    rw::Texture* m_texture = nullptr; // one reference held
};

} // namespace coney::platform
