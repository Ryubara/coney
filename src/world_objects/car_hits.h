// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "animation/anim_math.h"

namespace coney::world_objects {

struct Car;

/// A set of car parts, one bit per part id (bit `p` for part `p`).
using CarPartMask = std::uint32_t;

/// The window parts (15, 17, 19, 21): one hit breaks one (docs/research/cars.md#windows).
inline constexpr CarPartMask kCarWindowParts = (1U << 15U) | (1U << 17U) | (1U << 19U) | (1U << 21U);
/// The glass parts: the front and rear glass 6 and 7 and the windows (`Car_OnHit`'s mask `0xffd57f3f` leaves them out
/// of its sparks; docs/research/cars.md#hit-effects).
inline constexpr CarPartMask kCarGlassParts = (1U << 6U) | (1U << 7U) | kCarWindowParts;
/// The window beside the stereo: its break frees the stereo.
inline constexpr std::uint32_t kStereoWindowPart = 15;
/// A plain human hit's damage to each part it reaches.
inline constexpr float kHumanCarHitDamage = 0.115F;

/// Where a car is struck from, in the car's frame: x across (−x the left), y along (the front at +y), from the car's
/// position and turn.
[[nodiscard]] anim::Vec3 carLocal(const Car& car, anim::Vec3 world);

/// The parts a hit from a human standing at `standing` reaches, by the type's body and cabin zone tables (bit 1, the
/// roof, never). Research: docs/research/cars.md#windows.
/// @orig 0x0038b8d0 Car_HitZoneParts (unknown)
[[nodiscard]] CarPartMask carZoneParts(const Car& car, anim::Vec3 standing);

/// The parts a human at `standing` facing `forward` can hit: none unless he stands outside the car's box and faces its
/// nearest face (his forward · the face's outward normal ≤ −0.7); then carZoneParts().
/// @orig 0x0038b520 Car_FacingParts (unknown)
[[nodiscard]] CarPartMask carFacingParts(const Car& car, anim::Vec3 standing, anim::Vec3 forward);

/// Whether `Player_PickTarget`'s car pass takes the car for a human at `feet` searching along `heading` (radians):
/// less than 1 m outside the car's box across and along, the car within the search's cone and 2 m in height (or 0-2 m
/// below a human standing on it).
/// @orig 0x0038e860 Cars_FindNear (unknown)
/// @orig 0x00279f50 Player_CarTargetFilter (unknown)
[[nodiscard]] bool carTargetable(const Car& car, anim::Vec3 feet, float heading);

/// The point square aims at on a car, for a human at `feet` facing `forward`: below the feet when the car's position
/// is lower than them; else, when he can hit parts not yet off, 1 m ahead of him, at his feet + 1.5 m for a window
/// (or the windscreen or rear window), + 1.0 m for the bonnet or boot, else at his feet. Nothing when no part is left
/// to hit.
/// @orig 0x0038c990 Car_AimPoint (unknown)
[[nodiscard]] std::optional<anim::Vec3> carAimPoint(const Car& car, anim::Vec3 feet, anim::Vec3 forward);

/// The parts a plain human hit from `standing` damages: carZoneParts() less the parts already off, and less each door
/// whose window is still whole (an intact window shields its door).
/// @orig 0x0038bea0 Car_OnHit (unknown)
[[nodiscard]] CarPartMask carHumanHitParts(const Car& car, anim::Vec3 standing);

/// Where a car's stereo sits: the car's position plus its turn applied to (−0.75, 0.25, 0.1), the same for every type.
/// @orig 0x0038c868 Car_SpawnStereo (unknown)
[[nodiscard]] anim::Vec3 carStereoPosition(const Car& car);

/// Where a car's boot item sits: the car's position plus its turn applied to the type's boot offset (the sedan and
/// Sully's car (0, −2.34, −0.01), the police car (0, −2.26, −0.02), the coupe (0, −2.34, −0.05), the wagon and van 0).
/// @orig 0x0038bb48 Car_UpdateTransform (unknown)
[[nodiscard]] anim::Vec3 carBootPosition(const Car& car);

} // namespace coney::world_objects
