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
#include "gamemodes/game_mode.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/rumble_mode_gui/rumble_menu.h"
#include "gui/widget.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;

/// The level number of the arena the Rumble menu offers on a fresh boot: the Fight Pen, `level102`.
inline constexpr int kDefaultRumbleArena = 102;

/// The set-up the Rumble menu leaves when every screen's first entry is accepted on a fresh boot
/// (gui::rumbleMenuDefaults()): 1 ON 1, one player against the computer, the BASEBALL FURIES against the ORPHANS, in
/// the Fight Pen (docs/research/frontend.md#rumble-setup).
[[nodiscard]] RumbleSetup defaultRumbleSetup();

/// defaultRumbleSetup() for the arena `level` when `level` names a Rumble arena (`level101` to `level137`), with that
/// arena's level number; nothing for any other level. For `--play-level` of an arena, which has no menu before it
/// (**Coney's choice**: the menu offers only the Fight Pen on a fresh boot).
[[nodiscard]] std::optional<RumbleSetup> rumbleSetupForLevel(std::string_view level);

/// Game mode 0x11, the Rumble set-up menu that QUICK RUMBLE opens (`ShowRumbleModeInterface`): it keeps the cancel and
/// start callbacks, runs the menu's screens (gui::RumbleMenu: Game Mode, Game Type, Choose Gangs, Choose Area), which
/// write the set-up the arena reads (`GetRumbleModeData`, `GetRumbleModeGangName`) and the chosen arena's level number,
/// and on leaving calls the cancel callback or the start one with that level number
/// (docs/research/frontend.md#quick-rumble, docs/research/frontend.md#rumble-setup).
///
/// Coney's choices: the screens are drawn on black (there is no front-end world yet) with the fonts the profile
/// manager uses (`part_page0`, `big_font`), loaded on entry; a font that fails to load is logged and its text not
/// drawn. When the menu starts a fight, the profile manager below it (mode 0x12) is popped too, without its start game
/// callback, as its controller ends with Rumble mode chosen (docs/research/frontend.md#mode-flow); the level flow on
/// top then starts the arena. No autosave (Coney has no saves).
class RumbleMenuMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0x11;
    /// The text batches: how many sprites a frame, and their depth (as the profile manager's).
    static constexpr std::size_t kTextCapacity = 1024;
    static constexpr float kTextDepth = 9000.0F;

    /// Loads a sprite sheet by its resource name; the platform layer reads it from the disc.
    using SheetLoader = std::function<std::expected<graphics::SpriteSheet, Error>(std::string_view resourceName)>;

    /// Draws through `device` with fonts from `loadSheet`, pushes itself on `stack`, calls the callbacks in `scripts`
    /// and writes the set-up into `state`; each must outlive the mode. `log` gets a line for each step.
    RumbleMenuMode(graphics::RenderDevice& device, SheetLoader loadSheet, GameModeStack& stack,
                   script::ScriptSystem& scripts, GameState& state, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// `ShowRumbleModeInterface(onCancel, onStart, fromFrontEnd)`: keeps the two callbacks and whether the menu was
    /// opened from the front end, and pushes the mode unless it is on top.
    /// @orig 0x00155228 RumbleMenu_Show (unknown)
    void show(std::string onCancel, std::string onStart, bool fromFrontEnd);

    /// Clears the outcome and loads the fonts; the menu starts at its Game Mode screen on the next update.
    /// @orig 0x0015e8b0 Mode11::Enter (unknown)
    void enter() override;

    /// One step of the menu's screens; leaves when the menu is cancelled or the arena confirmed.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Draws the screen's text of the last step on black, then presents.
    void render(const RenderTime& time) override;

    /// Calls the cancel callback when cancelled, else the start callback with the arena's level number when started;
    /// releases the fonts.
    /// @orig 0x0015ea40 Mode11::Exit (unknown)
    void exit() override;

    /// Whether the last visit started a fight / was cancelled.
    [[nodiscard]] bool started() const { return m_started; }
    [[nodiscard]] bool cancelled() const { return m_cancelled; }
    /// Whether the menu was opened from the front end (`0x0063ef64`).
    [[nodiscard]] bool fromFrontEnd() const { return m_fromFrontEnd; }
    /// The menu's screens.
    [[nodiscard]] const gui::RumbleMenu& menu() const { return m_menu; }

  private:
    // Loads the font `name`; logs and returns nothing when it fails.
    std::optional<graphics::Font> loadFont(std::string_view name);
    // The text batch of font slot `slot`: big_font's for slot 6 when it loaded, else the text font's.
    [[nodiscard]] graphics::SpriteBatch* textBatch(int slot);

    graphics::RenderDevice& m_device;
    SheetLoader m_loadSheet;
    GameModeStack& m_stack;
    script::ScriptSystem& m_scripts;
    GameState& m_state;
    std::function<void(std::string_view)> m_log;
    gui::RumbleMenu m_menu;
    gui::GuiCanvas m_canvas;
    std::optional<graphics::Font> m_textFont;
    std::optional<graphics::Font> m_bigFont;
    std::optional<graphics::SpriteBatch> m_textBatch;
    std::optional<graphics::SpriteBatch> m_bigBatch;
    graphics::OverlayCamera m_camera;
    graphics::OverlayPass m_pass;
    std::string m_onCancel;        // `0x005e67c0`
    std::string m_onStart;         // `0x005e67c4`
    std::string_view m_lastScreen; // the screen logged last, so each change is logged once
    bool m_fromFrontEnd = false;
    bool m_startPending = false; // enter() ran; the menu starts on the next update, which knows the time
    bool m_started = false;      // `0x0050f4e0`
    bool m_cancelled = false;    // `0x0050f4dc`
};

} // namespace coney
