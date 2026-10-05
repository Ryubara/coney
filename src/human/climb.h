// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "raycast/collision_mesh.h"

// The player's climbs: which obstacle triangle climbs (a fence, a short fence, a wall or a short wall), from two
// forward rays and a downward probe just behind the face, and where the climb starts. Pure functions over the
// collision mesh; the human (src/human/human.*) plays the climb.
// Research: docs/research/characters.md#climb

namespace coney::human {

/// The climb's values the debug menus may edit while the game runs (docs/guides/debug-menu.md#tunables). Each
/// defaults to the researched constant; read through climbTuning().
struct ClimbTuning {
    float lowProbe = 0.69F;       ///< The low forward ray's height above the feet, m.
    float highProbe = 1.7F;       ///< The high forward ray's height, m.
    float reach = 1.5F;           ///< The forward rays' length, m.
    float runningReach = 4.5F;    ///< Their length for a player at the run or sprint gait, m.
    float tallTolerance = 0.1F;   ///< Both rays hitting closer together than this is one tall face, m.
    float behind = 0.4F;          ///< The downward probe's distance behind the face, m.
    float fenceTopLimit = 0.25F;  ///< A top behind the face at or above this, outside the window, refuses, m.
    float fenceCeiling = 2.5F;    ///< A tall fence must be lower than this (a ray from here must miss), m.
    float tallProbeHeight = 3.0F; ///< The downward probe's start for a tall face, m.
    float tallWindowLow = 1.7F;   ///< A tall face's wall top window, m.
    float tallWindowHigh = 2.91F;
    float shortProbeHeight = 1.8F; ///< The downward probe's start for a short face, m.
    float shortWindowLow = 0.7F;   ///< A short face's wall top window, m.
    float shortWindowHigh = 1.7F;
    float facing = -0.7F;         ///< The face must look at the climber: n · d below this.
    float standShare = 0.4F;      ///< A standing climb's face is at most (r1 + r2) × this away.
    float runShare = 2.2F;        ///< A running climb's face is at most r2 × this away.
    float startShare = 0.9F;      ///< The start point is the reach × this in front of the face.
    float runningReprobe = 2.5F;  ///< The forward ray at the first clip's end, from a run, m.
    float standingReprobe = 1.5F; ///< The same, standing, m.
};

/// The one ClimbTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] ClimbTuning& climbTuning();

/// The material whose triangles are climbable whatever their flags: 30, `LOW_FENCE`.
inline constexpr std::uint8_t kClimbableMaterial = 30;
/// Triangle flag bit 2: climbable by a player.
inline constexpr std::uint16_t kTrianglePlayerClimbable = 0x0004;
/// Triangle flag bit 7: climbable by anyone.
inline constexpr std::uint16_t kTriangleClimbable = 0x0080;

/// The four climbs, each a chain of three clips from a standing and a running form.
enum class ClimbKind : std::uint8_t { Fence, ShortFence, Wall, ShortWall };

/// The first clip of a climb: 437 fence, 443 short fence, 449 wall, 455 short wall standing; three more for the
/// running form (440, 446, 452, 458). The chain is that id, id + 1 and id + 2.
[[nodiscard]] std::uint32_t climbFirstClip(ClimbKind kind, bool running);

/// What the probes found: the climb, the face and the top behind it.
struct ClimbProbe {
    ClimbKind kind = ClimbKind::Fence;
    float distance = 0.0F; ///< Along the probe direction to the face, m.
    anim::Vec3 normal;     ///< The face's unit normal (it looks at the climber).
    anim::Vec3 face;       ///< The face at the feet's height, straight ahead.
    float top = 0.0F;      ///< The top just behind the face above the feet (0 for a fence with nothing behind).
};

/// Whether the triangle a ray hit is climbable: material 30, or flag bit 7, or (for a player) flag bit 2.
[[nodiscard]] bool climbable(const raycast::RayHit& hit, bool player);

/// The climb `Climb_TryStart` finds from the feet at `feet` along the unit horizontal direction `direction`: two
/// forward rays `length` long from 1.7 m and 0.69 m above the feet (a tall face when both hit within 0.1 m of each
/// other, a short one when only the low one does, or both further apart); a climbable hit; then `Climb_ProbeTop`
/// (the face must look at the climber; a downward probe 0.4 m behind it finds the top: inside the window a wall, else
/// below 0.25 m a fence, and a tall fence must be lower than 2.5 m). Nothing when there is no climb.
/// @orig 0x002826f0 Climb_TryStart (unknown)
/// @orig 0x00282370 Climb_ProbeTop (unknown)
[[nodiscard]] std::optional<ClimbProbe> probeClimb(const raycast::CollisionMesh& mesh, anim::Vec3 feet,
                                                   anim::Vec3 direction, float length, bool player);

/// Whether a climbable triangle lies ahead within jumpTuning().climbableCheck of a ray from 1.7 m above the feet:
/// the jump's refusal near a climbable wall (`0x0021d228`).
[[nodiscard]] bool climbableAhead(const raycast::CollisionMesh& mesh, anim::Vec3 feet, anim::Vec3 direction,
                                  float length, bool player);

/// Whether anything is ahead within `length` of a ray from 0.69 m above the feet: the probe at the first clip's end
/// that lets a climb go on (any triangle; **Coney's choice**, the page does not say it must be climbable).
[[nodiscard]] bool faceAhead(const raycast::CollisionMesh& mesh, anim::Vec3 feet, anim::Vec3 direction, float length);

/// A climb clip's reach: the length of its type-8 event's vector (`0x00101558`), or, for a clip with none, the length
/// of its root displacement (**Coney's fallback**; every climb clip on the disc has one).
[[nodiscard]] float clipReach(const anim::AnimClip& clip);

/// Whether a face `distance` away is within reach (`Climb_Start`): from a run between (r1 + r2) × 0.4 and r2 × 2.2,
/// standing at most (r1 + r2) × 0.4. **Coney's reading**: r1 and r2 are the reaches of the standing and the running
/// first clips (the page says "of the first and second clips"; this reading gives a running fence climb from 1.2 to
/// 5.1 m, which matches the runtime tap that started one at about 4.4 m, while the chain's first two clips would
/// give 1.15-1.25 m).
/// @orig 0x00281c20 Climb_Start (unknown)
[[nodiscard]] bool withinClimbReach(float distance, float standingReach, float runningReach, bool running);

} // namespace coney::human
