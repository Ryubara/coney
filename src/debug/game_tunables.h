// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "debug/tunables.h"

namespace coney::debug {

/// Registers the game's tunables in `registry`, each over the live value the game reads and defaulting to its
/// researched value:
///
/// - **Movement** (src/human/locomotion.h, docs/research/characters.md#movement-constants): the stick's dead zone,
///   the run threshold, the acceleration, the turn limit of each gait, and the lean's factors;
/// - **Body** (src/human/body.h, docs/research/characters.md#walls): the walking sphere and the 0.25 m step rule;
/// - **Sprint**, **Jump** and **Climb** (src/human/stamina.h, jump.h, climb.h, docs/research/characters.md#sprint,
///   #jump, #climb): stamina's maximum, drain and refill; the jump's speeds and checks; the climbs' probes and windows;
/// - **Follow camera** (src/camera/follow_camera.h, docs/research/camera.md): the position lag, the collision margins,
///   and the leash band, the pitch and the look-at height a new camera starts with.
///
/// Call it once at start-up; the values live as long as the program. Unregister with
/// TunableRegistry::removeCategory().
void registerGameTunables(TunableRegistry& registry);

} // namespace coney::debug
