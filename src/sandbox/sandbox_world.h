// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>

#include "core/error.h"
#include "raycast/collision_mesh.h"
#include "sandbox/sandbox_layout.h"
#include "sandbox/sandbox_lighting.h"
#include "sandbox/sandbox_mesh.h"

namespace coney::sandbox {

/// The collision grid's cell width for a sandbox, metres: a Coney choice, near the disc's narrowest cells (5 to 14 m,
/// docs/research/collision.md#grid) and fine enough that a cell holds few of a course's triangles.
inline constexpr float kSandboxCellSize = 4.0F;

/// A sandbox ready to use: its layout, the drawn geometry with the light baked in, and the collision mesh the player
/// and the cameras test against, in the same form a level's is (docs/research/collision.md). Platform-neutral: the
/// renderer (src/platform/sandbox_renderer.h) turns the mesh into librw geometry.
///
/// Coney's own feature, with no counterpart in the original game (docs/guides/sandbox.md).
class SandboxWorld {
  public:
    /// Builds the geometry (tessellated by the layout's `tessellate` edge), the collision mesh from the solid
    /// primitives, then bakes the light against that mesh. Fails as raycast::buildCollisionMesh() does, with
    /// ErrorCode::InvalidArgument when the solid primitives need more than raycast::kMaxCollisionTriangles triangles.
    [[nodiscard]] static std::expected<SandboxWorld, Error> build(SandboxLayout layout);

    /// loadSandboxLayout() then build(): the layout `nameOrPath` (a name in `folder`, or a file path), as the command
    /// line and the debug menu load one. Fails as findSandboxLayout(), loadSandboxLayout() and build() do.
    [[nodiscard]] static std::expected<SandboxWorld, Error> load(const std::filesystem::path& folder,
                                                                 std::string_view nameOrPath);

    /// build(), for a layout whose textures are in `folder`: a layout read from there and changed since (the debug
    /// menu's Spawner adds primitives to one). Fails as build() does.
    [[nodiscard]] static std::expected<SandboxWorld, Error> build(SandboxLayout layout, std::filesystem::path folder);

    [[nodiscard]] const SandboxLayout& layout() const { return m_layout; }
    [[nodiscard]] const SandboxMesh& mesh() const { return m_mesh; }
    /// The collision mesh; null when the layout has nothing solid.
    [[nodiscard]] const raycast::CollisionMesh* collision() const { return m_collision.get(); }
    [[nodiscard]] const BakeStats& bakeStats() const { return m_bake; }
    /// The folder the layout was read from, where its textures are; empty for a layout built from text.
    [[nodiscard]] const std::filesystem::path& folder() const { return m_folder; }

  private:
    SandboxWorld() = default;

    SandboxLayout m_layout;
    SandboxMesh m_mesh;
    std::unique_ptr<raycast::CollisionMesh> m_collision;
    BakeStats m_bake;
    std::filesystem::path m_folder;
};

} // namespace coney::sandbox
