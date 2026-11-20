// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD's standing state as network play reads and writes it (docs/guides/snapshots.md#hud-standing). Coney's own;
// the original has no network play.
#include <cstddef>

#include "hud/counter_panels.h"
#include "hud/hud.h"
#include "hud/player_panel.h"
#include "hud/scripted_bars.h"

namespace coney::hud {

PanelFlags PlayerPanel::flags() const { return {}; }

void PlayerPanel::setFlags(const PanelFlags& /*flags*/) {}

void CounterPanels::restore(std::size_t /*index*/, const CounterPanel& /*panel*/) {}

void ScriptedBars::restoreBar(std::size_t /*slot*/, const GenericBar& /*bar*/) {}

HudStanding Hud::standing() const { return {}; }

void Hud::applyStanding(const HudStanding& /*standing*/) {}

} // namespace coney::hud
