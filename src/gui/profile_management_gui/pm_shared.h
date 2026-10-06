// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "graphics/screen_fade.h"
#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/widget.h"
#include "warriors/game_state.h"
#include "warriors/profile_store.h"

namespace coney::gui {

/// Where the profile manager's screens put things, in GUI units. The original picks ten layout floats
/// (`0x0050f5c4`-`0x0050f5e8`) by video mode when the controller starts (`0x00203b98`); their values and which value
/// places what are not on the page, so every value here is Coney's choice, picked to read well on the 4:3 screen.
struct PmLayout {
    float logoX = 0.5F;        ///< PM_Greet's logo: centre x.
    float logoY = 0.32F;       ///< PM_Greet's logo: centre y.
    float logoWidth = 0.5F;    ///< PM_Greet's logo: GUI width (its height keeps the rectangle's shape).
    float promptY = 0.72F;     ///< PM_Greet's "press START" text: centre line.
    float menuTop = 0.55F;     ///< PM_Mode's first item: centre line.
    float menuRowGap = 0.08F;  ///< PM_Mode's distance between items.
    float usageY = 0.84F;      ///< The usage line: its first line's centre line.
    float textBoxWidth = 0.8F; ///< The box texts are centred in.
    float textScale = 1.0F;    ///< Font scale of prompts and items.
};

/// The profile manager's globals that its screens set for the mode and the save system to read (`0x0050f584`-
/// `0x0050f5c0` and the name at `0x0063f1d8`), reset when the controller starts. Meanings are inferred from their
/// uses (docs/research/frontend.md#profile-manager).
struct PmSession {
    std::optional<std::size_t> slot; ///< `0x0050f594`: the chosen profile slot.
    bool createOnExit = false;       ///< `0x0050f598`: the mode's exit creates the profile `name` in `slot`.
    bool deleteMode = false;         ///< `0x0050f5a0`: PM_Load lists profiles to delete.
    bool done = false;               ///< `0x0050f5b0`: the menus are done; the story starts.
    bool newGame = false;            ///< `0x0050f5b4`: a new game was started (the autosave check reads it).
    bool widescreen = false;         ///< `0x0050f5c0`: the 16:9 choice the mode's exit applies.
    std::string name;                ///< `0x0063f1d8`: the profile name being made.
};

/// What every screen of the profile manager shares (the original's flow keeps an `SFC_SharedData`): the strings, the
/// canvas and the menu sprite batch, the HUD player's input, the frame being run, the screen fade, and the profile
/// manager's ways out to sound and to the scripts (gamemodes/front_end_services.h).
///
/// Research: docs/research/frontend.md#profile-manager
struct PmShared {
    const GlobalStrings* strings = nullptr;       ///< The UI strings; null shows empty texts.
    GuiCanvas canvas;                             ///< Fonts and text batches.
    graphics::SpriteBatch* menuSprites = nullptr; ///< The `menu_system` instance; null draws no sprites.
    GuiFrame frame;                               ///< This frame's game time and the HUD player's pad.
    MenuInput input;                              ///< The HUD player's menu commands.
    PmLayout layout;                              ///< Where things go.
    bool europe = false;                          ///< The device flag 0x02, which hides PM_Extras.
    bool finishing = false;                       ///< The profile manager is finishing (ends PM_Greet's blink).
    PmSession session;                            ///< The globals the screens set.
    GameState* state = nullptr;                   ///< The game state the screens write; null writes nothing.
    ProfileStore* profiles = nullptr;             ///< The save system's profiles; null: no profile and no room.
    const Pad* secondPad = nullptr;               ///< Port 2's pad (PM_NumPlayers' player 2); may be null.
    const graphics::ScreenFade* fade = nullptr;   ///< The screen fade the menus wait for; null for none.
    std::size_t connectedPads = 1;                ///< Pads plugged in (PM_Mode's story goes to PM_NumPlayers from 2).
    std::string onRumble; ///< The first Lua callback the controller was started with (`Menu.fadeToRMI`).
    std::function<void(int cue)> playSound; ///< Plays a front-end sound cue; may be empty.
    /// Calls a Lua function by name with number arguments; may be empty.
    std::function<void(std::string_view function, std::span<const double> args)> callScript;

    /// Global string `id`, or an empty string without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const {
        return strings != nullptr ? strings->get(id) : std::string_view{};
    }
};

} // namespace coney::gui
