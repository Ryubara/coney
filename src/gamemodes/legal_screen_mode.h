// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"

namespace coney {

/// The game's languages, in the order of the original's language field (`W_GameState + 0x120`).
enum class Language : std::uint8_t {
    English,
    Spanish,
    French,
    Italian,
    German,
};

/// What picks the legal screen's picture: the language, the 16:9 option and the device's mode flag 0x02 (read as PAL,
/// speculative). The defaults are the NTSC-U disc's: English, 4:3, flag clear.
struct LegalScreenSettings {
    Language language = Language::English;
    bool widescreen = false;
    bool europe = false; ///< The device's mode flag 0x02 (docs/research/graphics.md#open-questions).
};

/// The resource name of the legal screen for `settings`: `legal_screen`, with `_w` for 16:9 and then `_sp`, `_fr`,
/// `_it` or `_ge` for a language other than English (`legal_screen_w_sp`); `legal_screen_euro` for English with the
/// flag 0x02.
///
/// Coney's choice where the page is silent: `legal_screen_euro` has no 16:9 variant on the disc, so the flag wins over
/// the 16:9 option for English.
///
/// Research: docs/research/graphics.md#first-screen
[[nodiscard]] std::string legalScreenResourceName(const LegalScreenSettings& settings);

/// The WAD file name of a resource: the decimal CRC-32 of its name (`"%u"`), so `legal_screen` is `863681355`.
///
/// Research: docs/research/graphics.md#first-screen, docs/research/formats/wad-contents.md#names
[[nodiscard]] std::string resourceFileName(std::string_view resourceName);

/// Game mode 5, the legal screen: the first screen after the start-up movies. It loads the legal screen's sprite
/// sheet, shows its first rectangle over the whole screen on black, holds it for 5,000 ms of game time whatever the
/// player presses, then leaves.
///
/// Coney's differences from the original, all invisible on the screen:
/// - The original draws the picture once in `Enter`, into both display buffers, and its `Update` presents nothing;
///   Coney draws and presents the same picture every frame, so a window that is moved, resized or captured still
///   shows it.
/// - The original times the hold in real milliseconds; Coney counts game time on the fixed 1/30 s step, so the hold is
///   exactly 150 frames and a test can run it without a clock.
/// - The picture fills the logical screen. The original sizes it from the overlay camera's near clip and per-mode
///   factors whose exact result is not worked out (docs/research/graphics.md#open-questions).
/// - The preload scripts the original runs in `Enter` wait for the Lua system.
///
/// Research: docs/research/graphics.md#first-screen, docs/research/frontend.md#mode-flow
class LegalScreenMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 5;
    /// How long the screen holds: the mode's minimum and maximum hold, both 5,000 ms.
    static constexpr std::uint64_t kHoldMilliseconds = 5000;

    /// Loads a sprite sheet by its resource name (`legal_screen`); the platform layer reads it from the disc.
    using SheetLoader = std::function<std::expected<graphics::SpriteSheet, Error>(std::string_view resourceName)>;

    /// Draws through `device` with the sheet `loadSheet` returns for `settings`' resource. A failed load is passed to
    /// `log` and the screen stays black for the hold. `device` must outlive the mode.
    LegalScreenMode(graphics::RenderDevice& device, SheetLoader loadSheet, LegalScreenSettings settings,
                    std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Loads the picture and starts the hold.
    /// @orig 0x00159a58 Mode5::Enter (unknown)
    /// @orig 0x00159c08 StartupScreen_Draw (unknown)
    void enter() override;

    /// Draws the frame and leaves once kHoldMilliseconds of game time have passed since the mode was entered. Input
    /// is not read: no button skips the screen.
    /// @orig 0x00159ae0 Mode5::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Releases the picture.
    /// @orig 0x00159ab8 Mode5::Exit (unknown)
    void exit() override;

    /// The sheet shown, while the mode is entered and the load succeeded.
    [[nodiscard]] const std::optional<graphics::SpriteSheet>& sheet() const { return m_sheet; }

  private:
    /// Clears the logical screen to black and draws the picture over it, then presents.
    void drawFrame();

    graphics::RenderDevice& m_device;
    SheetLoader m_loadSheet;
    LegalScreenSettings m_settings;
    std::function<void(std::string_view)> m_log;
    std::optional<graphics::SpriteSheet> m_sheet;
    std::optional<std::uint64_t> m_startTicks; // game time when the mode was entered; set by the first update
};

} // namespace coney
