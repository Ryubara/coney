// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "raycast/collision_mesh.h"

namespace coney::graphics {

/// The dimming of a human hiding in a shadow: while the brain's flag `+0x2d4` is set (the triangle under his feet has
/// type bit 4), his model colour's factor falls from 1.0 to 0.5 over 250 ms and his point lights are skipped; when it
/// is clear the factor is 1.0 at once. Stepped by the simulation with game time.
///
/// Research: docs/research/lighting.md#humans
/// @orig 0x00174320 HumanRender_Draw (HumanRender.cpp)
class ShadowDim {
  public:
    /// How long the fall takes, ms.
    static constexpr std::uint32_t kFallMs = 250;
    /// The factor at the end of the fall.
    static constexpr float kDimmest = 0.5F;

    /// One step of `elapsedMs` with the flag `hidden`.
    void step(bool hidden, std::uint32_t elapsedMs);
    /// The factor the human's colour (`HuColor`) is multiplied by.
    [[nodiscard]] float factor() const;
    /// Whether the flag is set: the human's point lights are skipped.
    [[nodiscard]] bool hidden() const { return m_hidden; }

  private:
    bool m_hidden = false;
    std::uint32_t m_hiddenMs = 0; // how long the flag has been set
};

/// The collision triangle type bit that marks a shadow to hide in (`flags` bit 4).
inline constexpr std::uint16_t kTriangleShadow = 0x10;

/// Whether the ground under `feet` (game axes) is a shadow to hide in: a ray straight down from 0.25 m above the feet
/// meets, within 4 m, a triangle with kTriangleShadow. **Coney's stand-in** for the original's ground check
/// (`0x0023eab8`, docs/research/characters.md), whose ray is not on the lighting page: the blob shadow's ray.
[[nodiscard]] bool onShadowGround(const raycast::CollisionMesh& mesh, raycast::Vec3 feet);

/// A human's blob shadow: one sprite of `part_page1` lying on the ground under him.
struct BlobShadow {
    raycast::Vec3 centre; ///< Game axes: 0.05 m above the ground hit.
    raycast::Vec3 normal; ///< The ground's unit normal: the sprite lies across it.
    float size = 0.0F;    ///< The square's side, metres.
};

/// The blob shadow sprite's rectangle in `part_page1`.
inline constexpr int kBlobShadowRect = 40;
/// Its colour (RGBA).
inline constexpr std::array<std::uint8_t, 4> kBlobShadowColour{10, 10, 10, 128};
/// **Coney's stand-in** for the shadow's size: the original scales it by the human's `+0x580`-`+0x588`, whose values
/// are not researched; 1 m square.
inline constexpr float kBlobShadowSize = 1.0F;

/// Where a human standing at `feet` (game axes) casts his blob shadow: a ray from 0.25 m above the feet, 4 m straight
/// down; on a hit, the sprite 0.05 m above the ground along its normal, `size` across. Nothing when the ray meets no
/// ground.
/// @orig 0x00174320 HumanRender_Draw (HumanRender.cpp)
[[nodiscard]] std::optional<BlobShadow> placeBlobShadow(const raycast::CollisionMesh& mesh, raycast::Vec3 feet,
                                                        float size = kBlobShadowSize);

} // namespace coney::graphics
