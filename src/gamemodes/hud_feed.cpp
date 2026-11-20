// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/hud_feed.h"

#include "combat/meters.h"
#include "combat/player_combat.h"
#include "human/fighter.h"

namespace coney {

void feedHumanToHud(hud::PlayerHudState& state, const human::Human& human) {
    // The rage meter's value, maximum and raging flag, which the panel and the health rings draw.
    const combat::RageMeter& rage = human.fighter().combat().rage();
    hud::PanelValues& values = state.panel;
    values.rage = rage.value();
    values.rageMax = rage.maximum();
    values.raging = rage.raging();
    // Knocked out by his health running out (once his dying reaction is over) or by a script (`Human_IsKnockedOut`),
    // which drops his Armies panel: Human::knockedOut(), never the script's mark alone.
    state.knockedOut = human.knockedOut();
}

} // namespace coney
