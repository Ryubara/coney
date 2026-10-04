// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.h"
#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"
#include "graphics/screen_fade.h"
#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/profile_management_gui/pm_controller.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "scripting/script_system.h"

namespace coney {

class GameModeStack;

/// Game mode 0x12, the profile manager: the front-end menus. Pushed by `ShowProfileManager` (show()), it runs the
/// profile manager's screen flow (gui::PmController) from PM_Greet, drawing it over the front-end scene, until the flow
/// is done.
///
/// - `Enter` plays the music `menu` unless it is already playing, loads the `menu_system` sprite sheet (a batch of 50
///   sprites at depth 8,500) and starts the controller with the first Lua callback. (Coney starts the controller at
///   the top of the first update, in the same step, so the first screen knows the frame's time.)
/// - `Update` runs one frame: the controller (the screen on top reads the HUD player's pad, port 1, and adds its
///   sprites), the 2D pass, the screen fade, then the scripts (scheduled calls) and the present. It leaves when the
///   flow is done.
/// - `Exit` stops the controller and, when the flow finished, calls the second Lua callback (`Menu.startGame`).
///
/// Coney's choices: there is no front-end world yet (`level100`'s world is not loaded), so the screen is cleared to
/// black where the original draws the Wonder Wheel scene; the fonts are loaded here (`part_page0` for font slot 2,
/// `big_font` for slot 6; the original makes them once at start-up) and drawn at depth 9,000; the Rumble-mode flag the
/// original's `Exit` reads is always clear; the fade is drawn over the menus as a black quad.
///
/// Research: docs/research/frontend.md#mode-flow, docs/research/frontend.md#profile-manager
class ProfileManagerMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0x12;
    /// The menu sprites' sheet and its batch (particle page header entry 3).
    static constexpr std::string_view kMenuSheet = "menu_system";
    static constexpr std::size_t kMenuCapacity = 50;
    static constexpr float kMenuDepth = 8500.0F;
    /// The text batches: how many sprites a frame, and their depth (big_font's text, docs/research/gui.md#draw-order).
    static constexpr std::size_t kTextCapacity = 2048;
    static constexpr float kTextDepth = 9000.0F;
    /// The music the front end plays.
    static constexpr std::string_view kMusic = "menu";

    /// Loads a sprite sheet by its resource name; the platform layer reads it from the disc.
    using SheetLoader = std::function<std::expected<graphics::SpriteSheet, Error>(std::string_view resourceName)>;

    /// Draws through `device` with sheets from `loadSheet`, shows `strings`, sends sound, movies and Lua calls to
    /// `services`, waits for `fade` and runs `scripts` once a frame; `europe` is the device flag 0x02 (hides
    /// PM_Extras). A sheet that fails to load is passed to `log` and its sprites or text are not drawn. Every reference
    /// must outlive the mode.
    ProfileManagerMode(graphics::RenderDevice& device, SheetLoader loadSheet, const gui::GlobalStrings& strings,
                       FrontEndServices& services, graphics::ScreenFade& fade, script::ScriptSystem& scripts,
                       bool europe, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// `ShowProfileManager(onRumble, onStartGame)`: keeps the two Lua callbacks and pushes this mode on `stack` unless
    /// it is already on top.
    /// @orig 0x001552b0 ShowProfileManager (unknown)
    void show(GameModeStack& stack, std::string onRumble, std::string onStartGame);

    /// Plays the music and loads the sheets; the controller starts at PM_Greet on the next update.
    /// @orig 0x0015e048 Mode12::Enter (unknown)
    void enter() override;

    /// One frame of the menus; leaves when the flow is done.
    /// @orig 0x0015e238 Mode12::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Stops the controller, calls the second callback when the flow finished, and releases the sheets.
    /// @orig 0x0015e130 Mode12::Exit (unknown)
    void exit() override;

    /// The controller: which screen is on top.
    [[nodiscard]] const gui::PmController& controller() const { return m_controller; }
    /// The first Lua callback show() kept (`Menu.fadeToRMI`).
    [[nodiscard]] const std::string& onRumble() const { return m_onRumble; }
    /// The second Lua callback show() kept (`Menu.startGame`).
    [[nodiscard]] const std::string& onStartGame() const { return m_onStartGame; }

  private:
    // Loads the menu sheet and the fonts, building their batches; logs what fails.
    void loadResources();
    // Loads the font `name`; logs and returns nothing when it fails.
    std::optional<graphics::Font> loadFont(std::string_view name);
    // The text batch of font slot `slot`: big_font's for slot 6 when it loaded, else the text font's.
    [[nodiscard]] graphics::SpriteBatch* textBatch(int slot);

    graphics::RenderDevice& m_device;
    SheetLoader m_loadSheet;
    FrontEndServices& m_services;
    graphics::ScreenFade& m_fade;
    script::ScriptSystem& m_scripts;
    std::function<void(std::string_view)> m_log;
    gui::PmShared m_shared;
    gui::PmController m_controller; // after m_shared, which it refers to
    std::optional<graphics::SpriteBatch> m_menuBatch;
    std::optional<graphics::Font> m_textFont;
    std::optional<graphics::Font> m_bigFont;
    std::optional<graphics::SpriteBatch> m_textBatch;
    std::optional<graphics::SpriteBatch> m_bigBatch;
    graphics::OverlayCamera m_camera;
    graphics::OverlayPass m_pass;
    std::string m_onRumble;
    std::string m_onStartGame;
    std::string m_lastScreen;    // the screen logged last, so each change is logged once
    bool m_finished = false;     // the flow emptied (the controller reported done)
    bool m_startPending = false; // enter() ran; the controller starts on the next update
};

} // namespace coney
