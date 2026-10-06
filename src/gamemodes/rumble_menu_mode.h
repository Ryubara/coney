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
#include "graphics/render_device.h"
#include "graphics/screen_fade.h"
#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/rumble_mode_gui/rumble_data.h"
#include "gui/rumble_mode_gui/rumble_menu.h"
#include "gui/widget.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;

/// The level number of the arena the Rumble menu offers on a fresh boot: the Fight Pen, `level102`.
inline constexpr int kDefaultRumbleArena = 102;

/// The level number of `level` when it names a Rumble arena (`level101` to `level137`); nothing for any other level.
/// For `--play-level` of an arena, which has no menu before it: the level runs with the set-up the menu leaves by
/// default (gui::rumbleMenuDefaults()) in that arena.
[[nodiscard]] std::optional<int> rumbleArenaOf(std::string_view level);

/// Game mode 0x11, the Rumble set-up menu that QUICK RUMBLE opens (`ShowRumbleModeInterface`): it keeps the cancel and
/// start callbacks, runs the menu's screens (gui::RumbleMenu: Game Mode, Game Type, Choose Gangs, Choose Area), which
/// write the set-up the arena reads (`GetRumbleModeData`, `GetRumbleModeGangName`) and the chosen arena's level number,
/// and on leaving calls the cancel callback or the start one with that level number
/// (docs/research/frontend.md#quick-rumble, docs/research/frontend.md#rumble-setup).
///
/// The screens' chunks run in `scripts`' current state, whose `CfgRumble*` bindings fill `data`
/// (docs/research/frontend.md#rumble-data). Backing out of the first screen sets "cancelled" only when the menu was
/// opened from the front end, as the original does; opened in game, the menu just closes.
///
/// As the original (docs/research/frontend.md#rumble-screens): on its first frame the mode queues a 0.7 s fade in (the
/// script's fade out left the screen black); once the screens end, they freeze, and a start stops the music and fades
/// out over 1.5 s, a cancel from the front end over 0.7 s; the mode pops when the fade has run. The screens' sound cues
/// go to the front end's audio.
///
/// Coney's choices: the screens are drawn on black under their opaque background picture (the original draws the
/// world behind it through a locked camera, `RM_Camera`, that only the gang screen's 3D fighters show; Coney has
/// neither yet) with the fonts the profile manager uses (`part_page0`, `big_font`), loaded on entry; a sheet that fails
/// to load is logged and its sprites not drawn. The background's sheet is found by its WAD file name, the decimal of
/// its name's CRC-32 `0x349348bd` (kBackgroundSheet), its name not being known. When the menu starts a fight, the
/// profile manager below it (mode 0x12) is popped too, without its start game callback, as its controller ends with
/// Rumble mode chosen (docs/research/frontend.md#mode-flow); the level flow on top then starts the arena. No autosave
/// (Coney has no saves).
class RumbleMenuMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0x11;
    /// The text batches: how many sprites a frame, and their depth (as the profile manager's).
    static constexpr std::size_t kTextCapacity = 1024;
    static constexpr float kTextDepth = 9000.0F;
    /// The background picture's sheet: sheet-table record 12, by its WAD file name (see above), and its batch.
    static constexpr std::string_view kBackgroundSheet = "882067645";
    static constexpr std::size_t kBackgroundCapacity = 4;
    static constexpr float kBackgroundDepth = 8000.0F;
    /// The fades: in once the screens are up, out when a fight starts or the menu is cancelled.
    static constexpr double kFadeInSeconds = 0.7;
    static constexpr double kStartFadeOutSeconds = 1.5;
    static constexpr double kCancelFadeOutSeconds = 0.7;

    /// Loads a sprite sheet by its resource name; the platform layer reads it from the disc.
    using SheetLoader = std::function<std::expected<graphics::SpriteSheet, Error>(std::string_view resourceName)>;

    /// Draws through `device` with sheets from `loadSheet`, pushes itself on `stack`, runs the screens' chunks and
    /// calls the callbacks in `scripts`, writes the set-up into `state`, reads the screens' text from `strings` and the
    /// lists from `data`, asks `services` for music and cues and fades through `fade`; each must outlive the mode.
    /// `europe` is the video flag 0x02 the layout follows. `log` gets a line for each step.
    RumbleMenuMode(graphics::RenderDevice& device, SheetLoader loadSheet, GameModeStack& stack,
                   script::ScriptSystem& scripts, GameState& state, const gui::GlobalStrings& strings,
                   gui::RumbleData& data, FrontEndServices& services, graphics::ScreenFade& fade, bool europe,
                   std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// `ShowRumbleModeInterface(onCancel, onStart, fromFrontEnd)`: keeps the two callbacks and whether the menu was
    /// opened from the front end, and pushes the mode unless it is on top.
    /// @orig 0x00155228 RumbleMenu_Show (unknown)
    void show(std::string onCancel, std::string onStart, bool fromFrontEnd);

    /// Clears the outcome and loads the fonts; the menu starts at its Game Mode screen on the next update.
    /// @orig 0x0015e8b0 Mode11::Enter (unknown)
    void enter() override;

    /// One step of the menu's screens and the fade; once the menu is cancelled or the arena confirmed, fades out and
    /// leaves when the fade has run.
    /// @orig 0x0015eca0 Mode11::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Draws the background and the screen's text of the last step on black, then the fade, and presents.
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
    FrontEndServices& m_services;
    graphics::ScreenFade& m_fade;
    gui::RumbleMenu m_menu;
    gui::GuiCanvas m_canvas;
    std::optional<graphics::Font> m_textFont;
    std::optional<graphics::Font> m_bigFont;
    std::optional<graphics::SpriteBatch> m_backgroundBatch;
    std::optional<graphics::SpriteBatch> m_textBatch;
    std::optional<graphics::SpriteBatch> m_bigBatch;
    graphics::OverlayCamera m_camera;
    graphics::OverlayPass m_pass;
    std::string m_onCancel;        // `0x005e67c0`
    std::string m_onStart;         // `0x005e67c4`
    std::string_view m_lastScreen; // the screen logged last, so each change is logged once
    bool m_fromFrontEnd = false;
    bool m_startPending = false; // enter() ran; the menu starts on the next update, which knows the time
    bool m_leaving = false;      // the screens ended: frozen while the fade out runs
    bool m_started = false;      // `0x0050f4e0`
    bool m_cancelled = false;    // `0x0050f4dc`
};

} // namespace coney
