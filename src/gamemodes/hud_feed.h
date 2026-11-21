// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hud/hud.h"
#include "human/human.h"

namespace coney {

/// Gives a player slot's HUD state what his human holds: his rage meter (its value, maximum and whether he rages) and
/// whether he is knocked out (Human::knockedOut(), `Human_IsKnockedOut`: his health run out and his dying reaction
/// over, or a script's `HuSetConscious(false)`), which drops his Armies panel. The play mode calls it each frame for
/// the slot's state, which then goes into the HUD frame (hud::applyPlayerHud()); a unit test pins what it reads
/// (docs/research/armies.md#the-join-messages, docs/research/combat-reactions.md#knockout-timing).
/// @coney-only glue between the human and the HUD's per-slot state; the original's panels read the human themselves
void feedHumanToHud(hud::PlayerHudState& state, const human::Human& human);

} // namespace coney
