// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/interpolation.h"
#include "core/pads.h"
#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/pause_menu/pause_menu.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;
class GameplayMode;
class LevelFlowMode;

/// Loads a sprite sheet by its resource name (`part_page0`), the platform reading it from the disc.
using PauseSheetLoader = std::function<std::expected<graphics::SpriteSheet, Error>(std::string_view resourceName)>;
/// Loads the sprite sheet of a record of the sprite-sheet table (docs/research/gui.md#sprite-sheet-table-chunk-0x4d-
/// particle-page-header): what the original's sprite words name in their high half.
using PauseRecordLoader = std::function<std::expected<graphics::SpriteSheet, Error>(std::uint32_t record)>;

/// What the pause and mission-failed modes need from the game beyond the modes: every hook may be empty.
struct PauseHooks {
    /// Pauses (true) or resumes (false) every sound playing; sounds started while paused (the menu's cues) play.
    std::function<void(bool paused)> pauseSound;
    /// The three objective lists (current, bonus, overview) `HUDSetObjective` filled.
    std::function<std::array<std::vector<std::string>, 3>()> objectives;
    /// Turns both radars off (`0x001b2658`), when the pause menu opens.
    std::function<void()> radarsOff;
};

/// The loaded fonts and batches a pause-style menu draws with, and the 2D pass over them: one per mode.
///
/// Coney's choices, as ProfileManagerMode's: the fonts are loaded when the mode is entered (`part_page0` for every font
/// slot but 6, `big_font` for slot 6), their text drawn at depth 9,000.
class MenuLayer {
  public:
    MenuLayer() = default;
    ~MenuLayer() = default;
    MenuLayer(const MenuLayer&) = delete;
    MenuLayer& operator=(const MenuLayer&) = delete;
    MenuLayer(MenuLayer&&) = delete;
    MenuLayer& operator=(MenuLayer&&) = delete;

    /// The text batches' capacity and depth.
    static constexpr std::size_t kTextCapacity = 2048;
    static constexpr float kTextDepth = 9000.0F;

    /// Loads the fonts through `loadSheet`, a sprite sheet of record `record` through `loadRecord` into a batch of
    /// `capacity` at `depth` (none when `record` is unset); `log` gets a line for what fails, prefixed `who`.
    void load(const PauseSheetLoader& loadSheet, const PauseRecordLoader& loadRecord,
              std::optional<std::uint32_t> record, std::size_t capacity, float depth,
              const std::function<void(std::string_view)>& log, std::string_view who);
    /// Releases everything load() made.
    void release();

    /// The canvas widgets render into.
    [[nodiscard]] const gui::GuiCanvas& canvas() const { return m_canvas; }
    /// The sprite batch of the record's sheet; null when it did not load.
    [[nodiscard]] graphics::SpriteBatch* sprites() { return m_sprites ? &*m_sprites : nullptr; }

    /// Empties the pass (the step's sprites are listed again).
    void begin() { m_pass.empty(); }
    /// Queues every batch for drawing.
    void queue();
    /// Draws the queued sprites through `device`.
    void draw(graphics::RenderDevice& device) { m_pass.draw(device, m_camera); }

  private:
    std::optional<graphics::Font> m_textFont;
    std::optional<graphics::Font> m_bigFont;
    std::optional<graphics::SpriteBatch> m_textBatch;
    std::optional<graphics::SpriteBatch> m_bigBatch;
    std::optional<graphics::SpriteBatch> m_sprites;
    graphics::OverlayCamera m_camera;
    graphics::OverlayPass m_pass;
    gui::GuiCanvas m_canvas{
        .fonts = [this](int slot) -> const graphics::Font* {
            if (slot == gui::kBigFontSlot && m_bigFont) {
                return &*m_bigFont;
            }
            return m_textFont ? &*m_textFont : nullptr;
        },
        .textBatch = [this](int slot) -> graphics::SpriteBatch* {
            if (slot == gui::kBigFontSlot && m_bigBatch) {
                return &*m_bigBatch;
            }
            return m_textBatch ? &*m_textBatch : nullptr;
        },
    };
};

/// Acts on what a pause or mission-failed menu ended with, once its mode is popped (`PauseMenu_Toggle`'s second half,
/// `0x00154f28`): Resume does nothing; RestartLevel puts the checkpoint back to 1 and RestartCheckpoint keeps it, and
/// both choose the current level again in `levelFlow` (by its record's name); QuitToHangout calls Lua
/// `runNextMission(0)`; the Rumble quits call `PauseGoToRMIQuick` or `PauseGoToRMIHangout`; QuitToMainMenu puts the
/// checkpoint to 1 and chooses the level `"menu"`, which no record is named, so the level flow brings the front end
/// back. Every choice but Resume then pops gameplay (mode 1) when it is on top of `stack`.
///
/// Coney's stand-in: the original sets `W_GameState + 0x14c` = 3 and mode 1's update leaves on it; Coney pops mode 1
/// here, as MissionCompleteMode does.
///
/// Research: docs/research/pause.md#leaving
/// @orig 0x00154f28 PauseMenu_Toggle (unknown)
void applyPauseOutcome(gui::PauseOutcome outcome, GameModeStack& stack, GameState& state, LevelFlowMode& levelFlow,
                       FrontEndServices& services, const std::function<void(std::string_view)>& log);

/// Game mode 0xa, the pause: START during play pushes it (toggle()); it freezes the game, pauses the sound and shows
/// the pause menu (gui::PauseMenu) over the world tinted to black, until the menu closes and pops it.
///
/// - **Enter** (`0x0015dbb8`): the game stops updating (gameplay is below this mode, so its update does not run: the
///   task manager's phase 1), all sound pauses, the sound bank `pause` is loaded (the previous one is remembered), the
///   menu opens for the pausing player and both radars turn off.
/// - **Update** (`0x0015dd98`): the menu's frame with the pausing player's pad; when the menu is closed, toggle() pops
///   the mode and acts on its outcome. Always stays: the menu pops the mode itself.
/// - **Render**: the world as it was when the game paused (gameplay's frame at alpha 1, its time frozen), then the
///   tint, then the menu's sprites over it.
/// - **Exit** (`0x0015dd38`): the previous sound bank is loaded again and the sound resumes.
///
/// Coney's stand-ins (docs/research/pause.md#coneys-implementation): the Armies of the Night menu is the story menu;
/// the mode's world frame (cameras, managers, the HUD's paused update) is not run; there is no pad vibration to stop;
/// the save on close (`+0x20b0`) is never wanted; the "saved sound state" is the sound bank; `ShowOptionMenu` (the
/// game state's `+0x11c`) is not modelled, so the menu always opens on the grid.
///
/// Research: docs/research/pause.md
class PauseMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0xa;
    /// The sound bank the pause loads.
    static constexpr std::string_view kSoundBank = "pause";
    /// The background's sheet record (docs/research/pause.md#layout-gui-coordinates) and its batch.
    static constexpr std::uint32_t kBackgroundRecord = 12;
    static constexpr std::size_t kBackgroundCapacity = 1;

    /// Draws through `device` with fonts from `loadSheet` and the background from `loadRecord`, shows `strings`, sends
    /// cues, banks and Lua calls to `services`, reloads levels through `state` and `levelFlow`, and draws the paused
    /// world through `world` (null: black). Every reference must outlive the mode; `log` gets a line per pause.
    PauseMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
              const gui::GlobalStrings& strings, FrontEndServices& services, GameState& state, LevelFlowMode& levelFlow,
              std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets the game's hooks (sound, objectives, radars).
    void setHooks(PauseHooks hooks) { m_hooks = std::move(hooks); }
    /// Draws `world` (not owned; null: black) under the menu.
    void setWorld(GameplayMode* world) { m_world = world; }

    /// What mode 1's update does for the pause each frame of play (`0x00158728`): the first connected pad whose START
    /// went down this step pauses the game for its player (toggle()); then the START cool-down counts down.
    /// @orig 0x00158728 Mode1::Update (unknown)
    void playFrame(GameModeStack& stack, const Pads& pads);

    /// `PauseMenu_Toggle`: nothing while the cool-down runs (returns false); pushes this mode for `player` when it is
    /// not on top; otherwise pops it, sets the cool-down and acts on the menu's outcome (applyPauseOutcome()).
    /// @orig 0x00154f28 PauseMenu_Toggle (unknown)
    bool toggle(GameModeStack& stack, int player);

    /// Pauses the sound, loads the bank, loads the fonts and the background and opens the menu on the next update.
    /// @orig 0x0015dbb8 PauseMode_Enter (unknown)
    void enter() override;
    /// The menu's frame; pops the mode when the menu has closed.
    /// @orig 0x0015dd98 PauseMode_Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The paused world, the tint and the menu.
    void render(const RenderTime& time) override;
    /// Restores the sound bank, resumes the sound, releases the fonts.
    /// @orig 0x0015dd38 PauseMode_Exit (unknown)
    void exit() override;

    /// The menu.
    [[nodiscard]] const gui::PauseMenu& menu() const { return m_menu; }
    /// The START cool-down (`+0x20ac`).
    [[nodiscard]] int cooldown() const { return m_cooldown; }
    /// The player who paused (`+0x1bc0`).
    [[nodiscard]] int player() const { return m_player; }
    /// Pauses since start-up.
    [[nodiscard]] std::uint64_t pauses() const { return m_pauses; }

  private:
    graphics::RenderDevice& m_device;
    PauseSheetLoader m_loadSheet;
    PauseRecordLoader m_loadRecord;
    const gui::GlobalStrings& m_strings;
    FrontEndServices& m_services;
    GameState& m_state;
    LevelFlowMode& m_levelFlow;
    std::function<void(std::string_view)> m_log;
    PauseHooks m_hooks;
    GameplayMode* m_world = nullptr;
    gui::PauseMenu m_menu;
    MenuLayer m_layer;
    std::string m_previousBank;
    RenderTime m_frozen;              // the world's frame as the game paused
    Interpolated<float> m_tint{0.0F}; // the tint at the last two steps
    int m_cooldown = 0;
    int m_player = 0;
    std::uint64_t m_pauses = 0;
    bool m_openPending = false;
};

} // namespace coney
