// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "animation/anim_math.h"

// The health rings: two flat rings on the ground round a player's feet, the outer showing health and the inner power
// (or rage while raging), and the outer ring of the player's target. Platform-neutral: the update decides who gets
// rings, when, and their arcs, colours, size and alpha; ringFan() makes a ring's triangles; src/platform draws them
// in the world pass after the blob shadows.
// Research: docs/research/hud.md#the-health-rings

namespace coney::hud {

/// Segments of a ring's circle (`0x00510500`).
inline constexpr int kRingSegments = 64;
/// The rings' radii to the rim at a hit pulse of 1, metres: 0.6 × 0.9 (outer) and 0.6 × 0.6 (inner).
inline constexpr float kOuterRingRadius = 0.54F;
inline constexpr float kInnerRingRadius = 0.36F;
/// Above the feet (`0x00510518`), and the step for each human whose rings were queued before (`0x00510558`), metres.
inline constexpr float kRingHeight = 0.055F;
inline constexpr float kRingStackStep = 0.001F;
/// The start angle past the camera's heading (`0x00510520`), radians.
inline constexpr float kRingStartOffset = 2.5F;
/// A rim vertex's texture offset from the rectangle's middle, whole-sheet units: u + 0.062 cos, v − 0.124 sin.
inline constexpr float kRingUvU = 0.062F;
inline constexpr float kRingUvV = 0.124F;
/// The `part_page1` rectangle the rings show (its middle) and the L1 marker's.
inline constexpr int kRingRect = 1;
inline constexpr int kTargetMarkerRect = 0;
/// The L1 marker's height above the target's feet and its size (× the hit pulse), metres.
inline constexpr float kTargetMarkerHeight = 0.041F;
inline constexpr float kTargetMarkerSize = 0.9F;

/// The show timings, ms: the hold after the last trigger before the fade-out, the fades, the low-health blink's
/// half-period (`0x00510510`), the rage-full flash's (`0x00510528`).
inline constexpr std::uint64_t kRingHoldMs = 4000;
inline constexpr std::uint64_t kRingFadeMs = 500;
inline constexpr std::uint64_t kRingBlinkMs = 232;
inline constexpr std::uint64_t kRageFlashMs = 200;
/// The rage-full flashes: gold, then normal, this many times.
inline constexpr int kRageFlashes = 3;
/// Alpha gained per ms of a fade (`0.51`: 255 over 500 ms).
inline constexpr float kRingFadePerMs = 0.51F;
/// The low-health blink's ceiling (`0x00510514`) and the health at or below which the rings stay up, percent.
inline constexpr float kRingBlinkPercent = 25.0F;
inline constexpr float kRingStayPercent = 20.0F;
/// The hit pulse: what its value moves per update towards its target (`0x0051051c`), and the largest scale.
inline constexpr int kPulseStep = 45;
inline constexpr float kPulseMaxScale = 1.15F;
/// The ring of the most players the show list holds (`0x006c72d8`).
inline constexpr std::size_t kRingListSize = 4;

/// An RGB colour of a ring's arc.
struct RingColour {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    friend bool operator==(const RingColour&, const RingColour&) = default;
};

/// The arcs' other colours: the lost part, the part beyond the capacity, the inner ring's power fill and rage fill,
/// the raging health fill, the rage-full flash.
inline constexpr RingColour kRingLost{0, 0, 0};
inline constexpr RingColour kRingRest{60, 60, 60};
inline constexpr RingColour kRingPower{100, 100, 100};
inline constexpr RingColour kRingRage{217, 158, 12};
inline constexpr RingColour kRingRaging{191, 191, 191};
inline constexpr RingColour kRingRageFlash{128, 100, 0};

/// The outer ring's fill for health `percent` (0-100): green above 75, near black at or below 0.1, otherwise
/// `(158, 24 + int(percent × 1.1571), 24)`, orange at 75 and red near 0.
/// @orig 0x0024a230 Reticule_QueueHealthRings (unknown)
[[nodiscard]] RingColour healthColour(float percent);

/// Which arc a rim vertex belongs to.
enum class RingArc : std::uint8_t { Fill, Lost, Rest };

/// The arc of rim vertex `k` (0-64) of a ring filled to `value` and lost to `capacity` (both 0-100): Rest when
/// 64 − k ≤ floor(64 × (100 − capacity) / 100) and that is above 0, else Lost when 64 − k ≤ floor(64 × (100 − value) /
/// 100), else Fill.
[[nodiscard]] RingArc ringArcOf(int k, float value, float capacity);

/// One ring to draw.
struct GroundRing {
    std::uint64_t id = 0; ///< The human it lies under (the drawer may put it under where the human is drawn).
    anim::Vec3 centre;    ///< Game axes, its height included.
    float lift = 0.0F;    ///< Its height above the feet, metres.
    float radius = 0.0F;  ///< To the rim, metres.
    float startAngle = 0.0F;
    float value = 0.0F;    ///< The fill's end, percent of the circle.
    float capacity = 0.0F; ///< The lost part's end, percent of the circle.
    RingColour fill;
    RingColour lost = kRingLost;
    RingColour rest = kRingRest;
    std::uint8_t alpha = 255;
};

/// The L1 marker under a non-player target: a flat, white sprite of `part_page1` rectangle 0.
struct TargetMarker {
    std::uint64_t id = 0;
    anim::Vec3 centre; ///< Game axes, kTargetMarkerHeight above the feet.
    float lift = kTargetMarkerHeight;
    float size = kTargetMarkerSize; ///< The square's side, metres.
    std::uint8_t alpha = 255;
    bool black = false; ///< Rumble's (black instead of white).
};

/// One vertex of a ring's triangles.
struct RingVertex {
    anim::Vec3 position; ///< Game axes.
    float du = 0.0F;     ///< Texture offset from the rectangle's middle, whole-sheet units.
    float dv = 0.0F;
    std::array<std::uint8_t, 4> rgba{};
};

/// A ring's 64 triangles from its centre (192 vertices), each in one colour: flat shading gives a triangle its last
/// vertex's, the rim vertex k + 1 (**Coney's reading** of which vertex leads; the arcs end on hard edges either way).
/// The centre vertex has the fill's colour.
[[nodiscard]] std::vector<RingVertex> ringFan(const GroundRing& ring);

/// A human as the rings see it on one update.
struct RingHuman {
    std::uint64_t id = 0;
    anim::Vec3 feet;              ///< Game axes.
    float healthPercent = 100.0F; ///< Health / maximum × 100, 0-100.
    float powerPercent = 100.0F;  ///< Power / its maximum × 100, 0-100 (players).
    int rage = 0;                 ///< The raw rage (`+0x650`), not divided by its maximum.
    bool raging = false;
    bool rageFull = false; ///< Rage at the Warrior class's maximum.
    int classByte = 35;    ///< The power class's byte `+0x40`: the rings span byte / 100 of the circle.
    bool canShow = true;   ///< Not airborne, in a scene, climbing over, down or dead.
    int damageTaken = 0;   ///< Damage this update (the hit pulse's target); 0 for none.
    bool player = false;
};

/// A player's part of an update.
struct RingPlayer {
    RingHuman human;
    bool selectPressed = false; ///< SELECT newly pressed.
    bool flashUsed = false;     ///< The HUD's panel request a flash sets.
    bool fightStance = false;   ///< The record's `+0x00` & 3.
    bool holdingL1 = false;     ///< The record's `+0x00` `0x8`.
    std::optional<RingHuman> target;
};

/// One update's input.
struct RingFrame {
    std::uint64_t nowMs = 0;
    bool hudShown = true;       ///< HUD `+0x177a0`: nothing while hidden (`HideHud`, a letterbox).
    bool forceAll = false;      ///< `HuForceEnableReticule`: every player's rings at 255.
    float cameraHeading = 0.0F; ///< atan2(x, y) of the player camera's forward, game axes.
    std::vector<RingPlayer> players;
};

/// The rings' update: who has rings this update and how they look. The queue holds this update's rings until the
/// next.
///
/// Research: docs/research/hud.md#the-health-rings
/// @orig 0x0024b780 Reticules_Update (unknown)
class HealthRings {
  public:
    /// One update (the task manager's, each 1/30 s step).
    void update(const RingFrame& frame);

    /// This update's rings and L1 markers, in their queue order.
    [[nodiscard]] const std::vector<GroundRing>& rings() const { return m_rings; }
    [[nodiscard]] const std::vector<TargetMarker>& markers() const { return m_markers; }
    /// The hit pulse's scale of human `id` (1 to kPulseMaxScale).
    [[nodiscard]] float pulseScale(std::uint64_t id) const;

  private:
    // A player in the show list.
    struct Shown {
        std::uint64_t id = 0;
        std::uint64_t firstSeen = 0;
        std::uint64_t lastTrigger = 0;
    };
    // A human's hit pulse (`+0x5d2` the target, `+0x5d4` the value).
    struct Pulse {
        int target = 0;
        int value = 0;
    };
    // A player's target and its rage-full flashes.
    struct PlayerState {
        std::uint64_t target = 0;
        std::uint64_t targetSince = 0;
        bool wasFull = false;
        std::optional<std::uint64_t> flashStart;
    };

    // The alpha of a listed player's rings now, 0 when none; drops an entry past its fade.
    std::uint8_t listAlpha(std::uint64_t id, std::uint64_t now);
    // Moves a human's pulse a step; returns its scale.
    float stepPulse(const RingHuman& human);
    // Queues the human's outer ring (and the inner one for a player).
    void queue(const RingHuman& human, const RingFrame& frame, std::uint8_t alpha, const PlayerState* state,
               bool inner);

    std::vector<Shown> m_list;
    std::vector<std::pair<std::uint64_t, Pulse>> m_pulses;
    std::vector<PlayerState> m_players;
    std::vector<GroundRing> m_rings;
    std::vector<TargetMarker> m_markers;
    int m_queued = 0; // humans whose rings were queued this update
};

} // namespace coney::hud
