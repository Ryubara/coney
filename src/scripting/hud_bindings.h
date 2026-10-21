// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "hud/hud.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"

namespace coney::script {

/// The HUD bindings Coney implements: every HUD binding the first mission calls, the few beside them that share their
/// state, and the end credits' (`PreloadCredits`, `ShowCredits`, `CfgCredits`). All real; installBindings() registers
/// them.
inline constexpr std::array<std::string_view, 41> kHudBindings{"CfgCredits",
                                                               "FlashRageBar",
                                                               "HUDSetChaseHUDState_DESTROY",
                                                               "ForceShowPlayerHud",
                                                               "HUDAddRadarHuman",
                                                               "HUDAddRadarMissionObjective",
                                                               "HUDAddSecondaryRadarMissionObjective",
                                                               "HUDCheckTutorialText",
                                                               "HUDDeleteRadarMissionObjective",
                                                               "HUDDeleteRadarObject",
                                                               "HUDEnableFixedCamIcon",
                                                               "HUDEnableGameTutorialText",
                                                               "HUDEnableInstArrow",
                                                               "HUDEnableTextProgress",
                                                               "HUDFlushTutorialText",
                                                               "HUDGetNewPH",
                                                               "HUDRadarSetRange",
                                                               "HUDReleasePH",
                                                               "HUDRemoveAllGoalText",
                                                               "HUDSetAnnounceMsg",
                                                               "HUDSetInstArrowAnimSpeed",
                                                               "HUDSetNumIndicator",
                                                               "HUDSetObjective",
                                                               "HUDSetPHValue",
                                                               "HUDSetRadarItemTexture",
                                                               "HUDSetRadarObjectFlash",
                                                               "HUDSetRadarZoomScale",
                                                               "HUDSetTextProgress",
                                                               "HUDSetTutorialCallback",
                                                               "HUDSetTutorialText",
                                                               "HUDShowMissionSummaryText",
                                                               "HUDTurnOffRadar",
                                                               "HUDTurnOnRadar",
                                                               "HidePlayerHud",
                                                               "HideHud",
                                                               "PreloadCredits",
                                                               "RestoreHud",
                                                               "ShowCredits",
                                                               "ShowHud",
                                                               "ShowPlayerHud",
                                                               "W_ShowStopWatch"};

/// What `HUDGetNewPH` returns when no panel is free (or there is no HUD): -1 as an unsigned 32-bit number.
inline constexpr double kNoCounterPanel = 4294967295.0;

/// Registers kHudBindings in `vm`, acting on `context.hud` (a null one does nothing: `HUDGetNewPH` then returns
/// kNoCounterPanel and `HUDCheckTutorialText` nil). `PreloadCredits` runs `credits.lua` through `scripts`.
///
/// Research: docs/research/hud.md, docs/references/bindings/hud.md
void addHudBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context);

/// The HUD's services from a binding context: the HUD, tutorial and announcement strings of `context.strings`, and the
/// HUD colours scripts gave `CfgHUDColor` and the interface cues `SoundCfgInterfaceSound` named (kept by their
/// bindings in `context.recorded`). The sound output is left empty (Hud::setSoundOutput()). Both must outlive
/// the services' use.
[[nodiscard]] hud::HudServices hudServicesOf(const BindingContext& context);

} // namespace coney::script
