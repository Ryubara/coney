// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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

/// The device's video-mode flags that pick the profile manager's layout (`*0x0050cdb4`): 0x02 (unidentified; it also
/// hides PM_Extras), 0x04 (16:9) and 0x20 (unidentified). Coney's screen is the default interlaced 4:3: none set.
struct PmVideoFlags {
    bool flag02 = false;     ///< 0x02.
    bool widescreen = false; ///< 0x04, 16:9.
    bool flag20 = false;     ///< 0x20.
};

/// The profile manager's ten layout floats (`0x0050f5c4`-`0x0050f5e8`), picked by video mode when the controller starts
/// (`0x00203b98`). GUI units; the PM screens are left-aligned at x, the left edge of the safe area by default.
///
/// Research: docs/research/frontend.md#pm-layout
struct PmLayout {
    float x = 0.0F;                                           ///< `0x0050f5c4`: every widget's x.
    std::array<float, 4> gridY{0.81F, 0.76F, 0.71F, 0.657F};  ///< `0x5c8`-`0x5d4`: grids of 1 to 4 rows.
    std::array<float, 4> titleY{0.745F, 0.70F, 0.65F, 0.60F}; ///< `0x5d8`-`0x5e4`: a title over 1 to 4 rows.
    float usageY = 0.87F;                                     ///< `0x5e8`: the usage line.

    /// The grid y for `rows` rows (1 to 4; clamped).
    [[nodiscard]] float gridFor(std::size_t rows) const;
    /// The title y over a grid of `rows` rows (1 to 4; clamped).
    [[nodiscard]] float titleFor(std::size_t rows) const;

    /// The floats for `flags`: 0x20 is tested first, then 0x02 (alone or with 0x04), then 0x04.
    /// @orig 0x00203b98 PM_PickLayout (PM_Controller.cpp)
    [[nodiscard]] static PmLayout forFlags(const PmVideoFlags& flags);
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
/// canvas and the menu sprite batches, the HUD player's input, the frame being run, the screen fade, the layout, the
/// session's globals, and the profile manager's ways out to sound and to the scripts (gamemodes/front_end_services.h).
///
/// Research: docs/research/frontend.md#profile-manager
struct PmShared {
    const GlobalStrings* strings = nullptr; ///< The UI strings; null shows empty texts.
    GuiCanvas canvas;                       ///< Fonts and text batches.
    /// The `menu_system` instance (`0x0050f588`, depth 8,500); null draws no sprites.
    graphics::SpriteBatch* menuSprites = nullptr;
    /// A `menu_system` instance at depth 11,000, for the sprite widgets that make their own there (PM_Greet's logo,
    /// PM_Light's square); null draws them nowhere.
    graphics::SpriteBatch* frontSprites = nullptr;
    GuiFrame frame;                             ///< This frame's game time and the HUD player's pad.
    MenuInput input;                            ///< The HUD player's menu commands.
    PmVideoFlags video;                         ///< The video-mode flags; 0x02 also hides PM_Extras.
    PmLayout layout;                            ///< The layout floats for `video`, picked when the controller starts.
    bool finishing = false;                     ///< The profile manager is finishing (ends PM_Greet's blink).
    PmSession session;                          ///< The globals the screens set.
    GameState* state = nullptr;                 ///< The game state the screens write; null writes nothing.
    ProfileStore* profiles = nullptr;           ///< The save system's profiles; null: no profile and no room.
    const Pad* secondPad = nullptr;             ///< Port 2's pad (PM_NumPlayers' player 2); may be null.
    const graphics::ScreenFade* fade = nullptr; ///< The screen fade the menus wait for; null for none.
    std::size_t connectedPads = 1;              ///< Pads plugged in (PM_Mode's story goes to PM_NumPlayers from 2).
    std::string onRumble; ///< The first Lua callback the controller was started with (`0x0050f584`, `Menu.fadeToRMI`).
    std::function<void(int cue)> playSound; ///< Plays a front-end sound cue; may be empty.
    /// Calls a Lua function by name with number arguments; may be empty.
    std::function<void(std::string_view function, std::span<const double> args)> callScript;

    /// Global string `id`, or an empty string without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const {
        return strings != nullptr ? strings->get(id) : std::string_view{};
    }
    /// Whether the screen fade is running or its level is not 0 (`0x005fdeb8 + 0x1d4`, `+0x1d8`).
    [[nodiscard]] bool fadeActive() const { return fade != nullptr && fade->active(); }
    /// Whether the fade's level is not 0 (`+0x1d8`): what most screens' handlers wait on.
    [[nodiscard]] bool fadeNotClear() const { return fade != nullptr && fade->level() != 0.0F; }
    /// Plays front-end sound cue `cue`, if the profile manager has a way to.
    void cue(int cue) const {
        if (playSound) {
            playSound(cue);
        }
    }
    /// Calls the Lua function `function` with `args`, if the profile manager has a way to.
    void call(std::string_view function, std::span<const double> args = {}) const {
        if (callScript && !function.empty()) {
            callScript(function, args);
        }
    }
};

} // namespace coney::gui
