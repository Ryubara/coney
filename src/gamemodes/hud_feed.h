// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "ai/brains.h"
#include "animation/anim_math.h"
#include "hud/crew_status.h"
#include "hud/hud.h"
#include "human/human.h"
#include "warriors/created_humans.h"

namespace coney {

/// Gives a player slot's HUD state what his human holds: his rage meter (its value, maximum and whether he rages) and
/// whether he is knocked out (Human::knockedOut(), `Human_IsKnockedOut`: his health run out and his dying reaction
/// over, or a script's `HuSetConscious(false)`), which drops his Armies panel. The play mode calls it each frame for
/// the slot's state, which then goes into the HUD frame (hud::applyPlayerHud()); a unit test pins what it reads
/// (docs/research/armies.md#the-join-messages, docs/research/combat-reactions.md#knockout-timing).
/// @coney-only glue between the human and the HUD's per-slot state; the original's panels read the human themselves
void feedHumanToHud(hud::PlayerHudState& state, const human::Human& human);

/// What the crew status list reads of every human with a brain, by handle: his gang, brain type, character type and
/// name (his `HuCreate` record's, from `created`, which may be null), whether he is knocked out or cuffed, whether
/// another brain holds him as an enemy and an attacker holds one of his slots, and his distance to `camera` (player
/// 1's camera, in the game's axes). The play mode hands it to the HUD's step each frame
/// (docs/research/hud-functions-parts.md#crew-status).
/// @coney-only glue between the brains and the HUD's crew status list; the original's list reads the humans itself
[[nodiscard]] std::vector<hud::CrewMember> feedCrewToHud(const ai::Brains& brains, const CreatedHumans* created,
                                                         const anim::Vec3& camera);

} // namespace coney
