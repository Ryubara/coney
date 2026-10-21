// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "animation/anim_math.h"

namespace coney::human {

/// A named place a player can be put: the feet, in the game's axes (z up, metres), and the heading in degrees (0
/// faces +y), as a level's player start and a sandbox spawn point are given. Coney's own: the play mode's places
/// (PlayScenery::places()) and its teleport take it, and so do the debug menus' Player page and the tests.
struct Place {
    std::string name;
    anim::Vec3 feet;
    float headingDegrees = 0.0F;
};

} // namespace coney::human
