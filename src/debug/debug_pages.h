// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string>
#include <string_view>

namespace coney::debug {

class DebugSession;
class MenuPage;
class TunableRegistry;

/// The Time page: pause, step one fixed step, slow motion, and the frame and step counts (with a channel to plot).
void addTimePage(DebugSession& session);
/// The Tunables page: one submenu per category of the registry, each tunable as a number or a toggle with its
/// default; reset all, save and load the overrides file.
void addTunablesPage(DebugSession& session);
/// The Natives page: the masterlist's categories, each binding with its Coney status, an argument editor built from
/// its signature, a call through the script state and the result.
void addNativesPage(DebugSession& session);
/// The Lua console page: a line to run in the script state, a file to run, and the output.
void addConsolePage(DebugSession& session);
/// The Cheats page: the retail cheat codes, each sent to the script's cheat callback as the code checker would.
void addCheatsPage(DebugSession& session);
/// The Levels page: the level table's levels, loaded by name.
void addLevelsPage(DebugSession& session);
/// The Player page: where the player is and how it moves (plotted), his character type and a change of it, freezing
/// it, and teleports to the scene's places, a typed spot or a saved one. Over PlayControls; says so when no player
/// plays.
void addPlayerPage(DebugSession& session);
/// The Camera page: the follow camera's eye and target, a reset behind the player, the free camera, and the Follow
/// camera tunables.
void addCameraPage(DebugSession& session);
/// The Spawner page: sandbox objects put in front of the player, and clearing them.
void addSpawnerPage(DebugSession& session);
/// The AI fighters page: spawn a fighter in front of the player, clear them, switch their engaging on or off, and watch
/// them. Over PlayControls.
void addFightersPage(DebugSession& session);
/// The Debug draw page: the lines of DebugDrawOptions.
void addDebugDrawPage(DebugSession& session);
/// Fills `page` with the tunables of `category`: each as a toggle or a number with its default, then a reset. The
/// Tunables page's submenus, and any page that shows one category.
void fillTunableCategory(MenuPage& page, TunableRegistry& registry, const std::string& category);

/// The Display page: the overlays of DisplayOptions.
void addDisplayPage(DebugSession& session);
/// The Audio page: the master and bus volumes, a test tone and the voices playing. Over AudioControls; says so when
/// the run has no sound.
void addAudioPage(DebugSession& session);
/// The Input page: port 1's buttons, sticks and pressures live, with channels to plot the sticks.
void addInputPage(DebugSession& session);

/// The 27 retail cheat codes in our words: what each does, by index (docs/research/debug.md#cheat-table).
inline constexpr std::array<std::string_view, 27> kCheatEffects{
    "Revives, spray cans and $200",
    "God mode",
    "Tireless",
    "Clear the wanted level",
    "None (inert code)",
    "Locked full rage",
    "Weapon: hunter",
    "Weapon: bat",
    "Weapon: pipe",
    "Weapon: machete",
    "None (inert code)",
    "Weapon: tough bat",
    "Complete the mission",
    "Unlock everything, complete the mission",
    "Clubhouse unlock 3",
    "Clubhouse unlock 6",
    "Clubhouse unlock 22",
    "Clubhouse unlock 27",
    "Clubhouse unlock 21",
    "Clubhouse unlock 26",
    "Clubhouse unlock 28",
    "Clubhouse unlock 9",
    "Clubhouse unlock 10",
    "Clubhouse unlock 23",
    "Clubhouse unlock 25",
    "Clubhouse unlock 30",
    "99 credits (Armies of the Night)",
};

/// The Lua function `global.lua` names as the cheat callback (`SetCheatCallback`), which the cheat checker calls with
/// the code's index (docs/research/debug.md#cheats).
inline constexpr std::string_view kCheatCallback = "DbgEnterCheat";

} // namespace coney::debug
