// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/widget.h"

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

/// What every screen of the profile manager shares (the original's flow keeps an `SFC_SharedData`): the strings, the
/// canvas and the menu sprite batch, the HUD player's input, the frame being run, and the profile manager's ways out
/// to sound and script, which Coney stands in for (gamemodes/front_end_services.h).
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
    std::function<void(int cue)> playSound;       ///< Plays a front-end sound cue; may be empty.
    std::function<void(std::string_view function, double argument)> callScript; ///< Calls a Lua function; may be empty.

    /// Global string `id`, or an empty string without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const {
        return strings != nullptr ? strings->get(id) : std::string_view{};
    }
};

} // namespace coney::gui
