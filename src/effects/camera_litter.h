// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "animation/anim_math.h"

// The blowing litter round the camera (docs/research/particles.md#garbage): `StartGarbage(kind)` arms 64 flat cards
// of litter on a grid round the camera, which fall, land, lie on the ground and are put back near the camera when it
// leaves them behind; `EndGarbage` stops it.

namespace coney::effects {

/// What a ray met: the point and the surface's unit normal.
struct LitterHit {
    anim::Vec3 point;
    anim::Vec3 normal;
};

/// Casts a ray from `from` to `to` against the level's collision; nothing when it meets nothing.
using LitterRay = std::function<std::optional<LitterHit>(anim::Vec3 from, anim::Vec3 to)>;

/// One piece of litter (a `0x70`-byte record).
struct LitterPiece {
    anim::Vec3 position;
    anim::Vec3 velocity;            ///< Metres a second (a 30 Hz update moves the piece by a 30th of it).
    std::uint16_t rect = 0;         ///< The rectangle of `part_page1` it draws.
    float size = 0.0F;              ///< Metres across (its size byte / 256).
    std::uint8_t grey = 0;          ///< Its grey, 128-190, in all three channels.
    float threshold = 0.0F;         ///< The wind speed it takes to lift it, 0.5-10.
    std::uint32_t lifetime = 0;     ///< Updates left, from 600-900.
    float tilt = 1.0F;              ///< 1 edge-on (as it starts, a quarter turn), 0 lying flat.
    float flattening = 0.0F;        ///< Tilt lost per update while it lies down (over up to 8 updates).
    bool grounded = false;          ///< It has touched the ground.
    bool placed = false;            ///< Just placed: the next ground ray drops it onto the hit.
    std::uint32_t fade = 0;         ///< Updates left of a fade out (from 60); 0 when not fading.
    std::uint32_t rayCountdown = 0; ///< Updates to its next ground ray (every 20, staggered).

    /// Its opacity, 0-255: 4.25 × the fade's countdown while fading out, else 255.
    [[nodiscard]] float alpha() const;
};

/// The litter round the camera (`ICameraGarbage` at `0x005971a0`).
///
/// **Coney's readings** where the page is silent: the wind vector (`0x006f31a0`, written by `Wind_Manager`, not read)
/// is taken as still, so no piece is ever lifted, tumbled or given the start's push; a piece's orientation is kept as
/// one tilt (1 edge-on to 0 flat) for the gravity rule; a respawned piece goes back to its own grid cell round the
/// camera at a new height. Kept, not drawn: Coney's particle renderer draws camera-facing sprites, not cards.
/// The generator is the litter's own xorshift32, so a run is the same every time.
class CameraLitter {
  public:
    /// Pieces of litter.
    static constexpr std::size_t kPieces = 64;
    /// Kinds 0 to kKinds - 1 start it; any other does nothing.
    static constexpr std::uint32_t kKinds = 4;
    /// The pieces step at 30 Hz.
    static constexpr float kUpdatesPerSecond = 30.0F;
    /// The start's grid: 8 × 8 cells 7 m apart, from −28 m round the camera.
    static constexpr int kGrid = 8;
    static constexpr float kCell = 7.0F;
    /// Heights round the camera's, metres.
    static constexpr float kMinHeight = -1.0F;
    static constexpr float kMaxHeight = 5.0F;
    /// Every this many updates a piece looks for the ground this far below.
    static constexpr std::uint32_t kRayUpdates = 20;
    static constexpr float kGroundRay = 25.0F;
    /// A piece further than this from the camera on x or y fades out over kFadeUpdates and respawns.
    static constexpr float kKeepWithin = 28.0F;
    static constexpr std::uint32_t kFadeUpdates = 60;
    /// Gravity: metres a second taken off the z velocity per update, edge-on (half that lying flat): 9.8 m/s².
    static constexpr float kGravity = 0.327F;
    /// The collision ray starts this far behind the piece and counts hits within kHitWithin.
    static constexpr float kRayBehind = 0.5F;
    static constexpr float kHitWithin = 0.7F;

    explicit CameraLitter(std::uint32_t seed = 0x2545F491U) : m_random(seed != 0 ? seed : 1U) {}

    /// `StartGarbage(kind)`: kinds 0-3 arm the pieces on the grid round the camera at the next step; others do
    /// nothing.
    /// @orig 0x00170330 Garbage_Start (unknown)
    void start(std::uint32_t kind);
    /// `EndGarbage()`: the litter stops (its active flag cleared).
    /// @orig 0x00170528 Garbage_End (unknown)
    void end();

    /// One step of `seconds` with the camera at `camera`, the 30 Hz updates due: gravity, the collision ray, the ground
    /// rays and the respawns. `ray` meets the level (empty: nothing is ever met, and pieces fall).
    /// @orig 0x00170c88 Garbage_Update (unknown)
    void step(float seconds, anim::Vec3 camera, const LitterRay& ray);

    /// The kind blowing, or nothing.
    [[nodiscard]] std::optional<std::uint32_t> kind() const { return m_kind; }
    /// The pieces (armed while a kind blows).
    [[nodiscard]] const std::array<LitterPiece, kPieces>& pieces() const { return m_pieces; }
    /// Whether the pieces have been placed since the start.
    [[nodiscard]] bool armed() const { return m_armed; }

  private:
    // A number in [0, 1), and a whole number from `low` to `high` (both included).
    float unit();
    int between(int low, int high);
    // Piece `index` made afresh in its grid cell round `camera`.
    void place(std::size_t index, anim::Vec3 camera);
    // One 30 Hz update of piece `index`.
    void update(std::size_t index, anim::Vec3 camera, const LitterRay& ray);

    std::optional<std::uint32_t> m_kind;
    std::array<LitterPiece, kPieces> m_pieces{};
    bool m_armed = false;
    float m_updates = 0.0F; // updates due, not yet run
    std::uint32_t m_random;
};

} // namespace coney::effects
