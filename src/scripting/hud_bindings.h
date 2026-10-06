// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "hud/hud.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The HUD bindings Coney implements: every HUD binding the first mission calls, and the few beside them that share
/// their state. All real; installBindings() registers them.
inline constexpr std::array<std::string_view, 34> kHudBindings{"FlashRageBar",
                                                               "ForceShowPlayerHud",
                                                               "HUDAddRadarHuman",
                                                               "HUDAddRadarMissionObjective",
                                                               "HUDAddSecondaryRadarMissionObjective",
                                                               "HUDCheckTutorialText",
                                                               "HUDDeleteRadarMissionObjective",
                                                               "HUDDeleteRadarObject",
                                                               "HUDEnableGameTutorialText",
                                                               "HUDEnableInstArrow",
                                                               "HUDEnableTextProgress",
                                                               "HUDFlushTutorialText",
                                                               "HUDGetNewPH",
                                                               "HUDReleasePH",
                                                               "HUDRemoveAllGoalText",
                                                               "HUDSetAnnounceMsg",
                                                               "HUDSetInstArrowAnimSpeed",
                                                               "HUDSetNumIndicator",
                                                               "HUDSetObjective",
                                                               "HUDSetPHValue",
                                                               "HUDSetRadarItemTexture",
                                                               "HUDSetRadarObjectFlash",
                                                               "HUDSetTextProgress",
                                                               "HUDSetTutorialCallback",
                                                               "HUDSetTutorialText",
                                                               "HUDShowMissionSummaryText",
                                                               "HUDTurnOffRadar",
                                                               "HUDTurnOnRadar",
                                                               "HidePlayerHud",
                                                               "HideHud",
                                                               "RestoreHud",
                                                               "ShowHud",
                                                               "ShowPlayerHud",
                                                               "W_ShowStopWatch"};

/// What `HUDGetNewPH` returns when no panel is free (or there is no HUD): -1 as an unsigned 32-bit number.
inline constexpr double kNoCounterPanel = 4294967295.0;

/// Registers kHudBindings in `vm`, acting on `context.hud` (a null one does nothing: `HUDGetNewPH` then returns
/// kNoCounterPanel and `HUDCheckTutorialText` nil).
///
/// Research: docs/research/hud.md, docs/references/bindings/hud.md
void addHudBindings(LuaVm& vm, const BindingContext& context);

/// The HUD's services from a binding context: the HUD, tutorial and announcement strings of `context.strings`, and the
/// HUD colours scripts gave `CfgHUDColor` and the interface cues `SoundCfgInterfaceSound` named (kept by their
/// recording stubs in `context.recorded`). The sound output is left empty (Hud::setSoundOutput()). Both must outlive
/// the services' use.
[[nodiscard]] hud::HudServices hudServicesOf(const BindingContext& context);

} // namespace coney::script
