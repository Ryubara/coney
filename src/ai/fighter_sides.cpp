// SPDX-License-Identifier: GPL-3.0-or-later
// The gangs of the debug menus' spawned fighters (docs/guides/debug-menu.md#pages). @coney-only debug tooling: nothing
// in the original corresponds. A stub: no fighter joins a gang yet.
#include "ai/fighter_sides.h"

namespace coney::ai {

int FighterSides::join(FighterSide /*side*/, Brain& /*fighter*/, const Brain& /*player*/) { return -1; }

std::optional<FighterSide> FighterSides::sideOf(const Gang* /*gang*/) const { return std::nullopt; }

} // namespace coney::ai
