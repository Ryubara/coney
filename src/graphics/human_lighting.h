// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "raycast/collision_mesh.h"
#include "world/view_frustum.h"

namespace coney::graphics {

/// The dimming of a human hiding in a shadow: while the brain's flag `+0x2d4` is set (the triangle under his feet has
/// type bit 4), his model colour's factor falls from 1.0 to 0.5 over 250 ms and his point lights are skipped; when it
/// clears the factor rises from 0.5 back to 1.0 over 250 ms (`f = 0.5 + t`, docs/research/graphics.md#human-draw).
/// Stepped by the simulation with game time.
///
/// Research: docs/research/lighting.md#humans
/// @orig 0x00174320 HumanRender_Draw (HumanRender.cpp)
class ShadowDim {
  public:
    /// How long the fall and the rise take, ms.
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
    std::uint32_t m_sinceChangeMs = kFallMs; // since the flag last changed, at most kFallMs (long ago at the start)
};

/// The collision triangle type bit that marks a shadow to hide in (`flags` bit 4, raycast::kTriangleShadow).
inline constexpr std::uint16_t kTriangleShadow = raycast::kTriangleShadow;

/// Whether the ground under `feet` (game axes) is a shadow to hide in: a ray straight down from 0.25 m above the feet
/// meets, within 4 m, a triangle with kTriangleShadow. **Coney's stand-in** for the original's ground check
/// (`0x0023eab8`, docs/research/characters.md), whose ray is not on the lighting page: the blob shadow's ray.
[[nodiscard]] bool onShadowGround(const raycast::CollisionMesh& mesh, raycast::Vec3 feet);

/// The humans' draw distance, metres (`0x0050cc60`, read at runtime in `level99`; `HumanRender_SetDrawDistance` keeps
/// it within 30-70 m).
inline constexpr float kHumanDrawDistance = 70.0F;
/// The humans' draw distance this frame, metres: the one source that the distance fade and the cull before posing
/// read, so a later render-side draw distance moves both. Always kHumanDrawDistance for now; the original clamps the
/// frame's value to 30-70 m (`HumanRender_SetDrawDistance` `0x00174200`), which is not ported yet.
/// @coney-only the single reader of the humans' draw distance; the original reads `0x0050cc60` where it needs it
[[nodiscard]] float humanDrawDistance();
/// Over how many metres before the draw distance a human fades out (`0x0050cc68`).
inline constexpr float kHumanFadeLength = 10.0F;

/// What a human's alpha is multiplied by at `distance` metres from the camera: 1 up to the draw distance less the fade
/// length (60 m), falling linearly to 0 at the draw distance (humanDrawDistance(), 70 m) and beyond.
///
/// Research: docs/research/graphics.md#human-draw
/// @orig 0x00174320 HumanRender_Draw (HumanRender.cpp)
[[nodiscard]] float humanDistanceFade(float distance);

/// The sphere that holds every pose of a human drawn at scale 1, and what he wears or holds: this far round a point
/// kHumanPoseLift above his feet, metres. **Coney's choice**: a standing body is under 2 m tall and a lying one under
/// 2 m long, so 3 m holds any of them, the arms and a held weapon included, with room to spare.
inline constexpr float kHumanPoseRadius = 3.0F;
/// How far above the feet the sphere's centre is, metres.
inline constexpr float kHumanPoseLift = 1.0F;

/// What cullHuman() says of a human before he is posed.
enum class HumanCull : std::uint8_t {
    MayShow,          ///< Some pose of his may be in the picture: pose and draw him.
    OutOfView,        ///< Every pose of his is behind the camera or beyond one of the view window's sides.
    PastDrawDistance, ///< In view, but every pose of his is past the draw distance (humanDistanceFade() is 0).
};

/// Whether a human standing at `feet` (RenderWare's axes, y up) and drawn at `scale` may show in a picture taken by
/// `camera` with a view window `halfWidth` × `halfHeight` at distance 1: his kHumanPoseRadius sphere (times the scale
/// when it is over 1) tested against the view's four sides, then against humanDrawDistance(). Conservative: it never
/// culls a human any pose of whom could be drawn, so the picture is the same as posing everyone; it saves posing the
/// ones that cannot (docs/engine/renderer-performance.md#cull-before-posing).
/// @coney-only the render's cull before posing; the original's visibility callback queues the humans its own camera
/// test passes (docs/research/graphics.md#human-draw)
[[nodiscard]] HumanCull cullHuman(const world::CameraPose& camera, float halfWidth, float halfHeight, world::Vec3 feet,
                                  float scale);

/// A human's blob shadow: one sprite of `part_page1` lying on the ground under him.
struct BlobShadow {
    raycast::Vec3 centre; ///< Game axes: 0.05 m above the ground hit.
    raycast::Vec3 normal; ///< The ground's unit normal: the sprite lies across it.
    float size = 0.0F;    ///< The square's side, metres.
};

/// The blob shadow sprite's rectangle in `part_page1`.
inline constexpr int kBlobShadowRect = 40;
/// Its colour (RGBA) on RenderWare's 0-255 scale, drawn as given: alpha 128 is half transparent.
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
