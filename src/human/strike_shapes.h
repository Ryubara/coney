// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "animation/anim_pose.h"

// A human's strike shapes: the ten bone shapes of its body (segments and spheres on the spine, head, forearms, hands,
// shins and feet), which its clips' events switch on and off, posed by the clip each update; the strike test meets
// other bodies with them. Research: docs/research/combat.md#moving-strikes

namespace coney::human {

/// One bone strike shape (`IPhysics_Construct`'s tables at `0x00512810`-`0x005128f0`): a segment from the bone's
/// position plus its rotated offset along the bone's local x for `length`, or a sphere at that point, of `radius`.
/// `target`: flag `0x4`, the shapes a strike is tested against on a human target (the spine and the head).
struct StrikeShapeDef {
    int bone = 0;
    bool segment = false;
    anim::Vec3 offset;
    float radius = 0.0F;
    float length = 0.0F;
    bool target = false;
};

/// The ten bone shapes every human has, by bone id: 3 spine, 6 head, 18 and 19 the left forearm and hand, 24 and 25
/// the right, 29 and 30 one shin and foot, 32 and 33 the other.
[[nodiscard]] std::span<const StrikeShapeDef> strikeShapeDefs();

/// The clip event types that switch them (`Anim_FireEvents` → `Human_HandleMessage`): one bone's shape on (its id in
/// the event's `+6` word) or off, every shape on (and the capsule's strike flag) or off.
inline constexpr std::uint16_t kEventStrikeOn = 0xf;
inline constexpr std::uint16_t kEventStrikeOff = 0x10;
inline constexpr std::uint16_t kEventStrikeAllOn = 0x13;
inline constexpr std::uint16_t kEventStrikeAllOff = 0x14;

/// A posed shape in the world: a segment from `a` to `b`, or a sphere at `a` (`b` equal to it), of `radius`.
struct PosedShape {
    anim::Vec3 a;
    anim::Vec3 b;
    float radius = 0.0F;
    int bone = 0;
};

/// Where a human's character space is in the world: its feet, its heading (radians, 0 facing +y), its lean into a turn
/// (Human::lean()) and its body scale, which scales the shapes' offsets, radii and lengths
/// (`PhysicsBody_ApplyHumanScale`). **Coney's reading**: the bones stand where the drawn body has them (unscaled,
/// leaned as the skin is), so the shapes cover the body the player sees.
struct BodyPlacement {
    anim::Vec3 feet;
    float heading = 0.0F;
    float lean = 0.0F;
    float scale = 1.0F;
};

/// Poses the shapes of `defs` from the bone transforms of the human's pose (character space, anim::boneTransforms())
/// into the world at `placement`.
[[nodiscard]] std::vector<PosedShape> poseStrikeShapes(std::span<const StrikeShapeDef> defs,
                                                       std::span<const anim::Mat34, anim::kPoseBones> bones,
                                                       const BodyPlacement& placement);

/// Whether two posed shapes overlap (their segments, or points, nearer than the sum of the radii).
[[nodiscard]] bool shapesOverlap(const PosedShape& a, const PosedShape& b);

/// Whether shape `to`, swept from where it was posed the update before (`from`, the same shape), meets `part`: the
/// test along each shape's move since the last update (`Human_TestStrikes` `0x0033f110` tests from a shape's previous
/// posed point, `+0x20`, to its new one, `+0x50`, in the world; docs/research/combat-moves.md#reach). A sphere sweeps
/// a capsule from its old centre to its new one. **Coney's reading** for a segment: its new place and the paths of
/// its two ends.
[[nodiscard]] bool sweptShapesMeet(const PosedShape& from, const PosedShape& to, const PosedShape& part);

/// Whether a posed shape overlaps the triangle `p0`, `p1`, `p2`.
[[nodiscard]] bool shapeTouchesTriangle(const PosedShape& shape, anim::Vec3 p0, anim::Vec3 p1, anim::Vec3 p2);

/// Which of a human's shapes are on (body `+0xc0`, a bit `1 << (id − 2)` per bone id), the capsule's strike flag, and
/// the bodies struck since they came on (each body is struck once while the shapes stay on).
class StrikeShapes {
  public:
    /// One clip event of the human's newest clip: the four strike types switch shapes; the others do nothing. Turning
    /// the last shape off forgets the bodies struck (**Coney's reading** of the contact list's emptying, `0x00342168`).
    /// @orig 0x00247fc0 Human_StrikeShapeOn (unknown)
    /// @orig 0x00248110 Human_StrikeShapeOff (unknown)
    /// @orig 0x00248170 Human_StrikeAllOn (unknown)
    /// @orig 0x00248270 Human_StrikeAllOff (unknown)
    void onEvent(const anim::ClipEvent& event);
    /// Every shape off (`PhysBody_SetShapeEnabled` with id −1), the struck bodies forgotten.
    void clear();

    /// Whether any shape is on, and whether bone `id`'s is.
    [[nodiscard]] bool anyOn() const { return m_bits != 0; }
    [[nodiscard]] bool on(int bone) const;
    /// The capsule's strike flag (`0x2`, set with all on).
    [[nodiscard]] bool capsuleOn() const { return m_capsule; }
    /// The definitions of the shapes on.
    [[nodiscard]] std::vector<StrikeShapeDef> active() const;

    /// Whether object `object` (its handle) or human `human` was struck since the shapes came on; marks one struck.
    [[nodiscard]] bool struck(double object) const;
    void markStruck(double object) { m_struckObjects.push_back(object); }
    [[nodiscard]] bool struckHuman(const void* human) const;
    void markStruckHuman(const void* human) { m_struckHumans.push_back(human); }

  private:
    // Sets or clears bone `id`'s bit (an id outside 2-33 is ignored).
    void set(int bone, bool on);

    std::uint32_t m_bits = 0;
    bool m_capsule = false;
    std::vector<double> m_struckObjects;
    std::vector<const void*> m_struckHumans;
};

} // namespace coney::human
