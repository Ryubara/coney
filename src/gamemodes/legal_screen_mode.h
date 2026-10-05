// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/error.h"
#include "core/language.h"
#include "gamemodes/game_mode.h"
#include "graphics/overlay_camera.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"
#include "scripting/script_system.h"

namespace coney {

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

/// The legal screen's scale factors (horizontal, vertical) for the video mode `settings` stands for: (1.55, 1.35) in
/// the default interlaced 4:3 mode, (1.9, 1.45) with the 16:9 option. The original also has factors for its
/// progressive modes, which Coney does not offer.
///
/// Research: docs/research/graphics.md#first-screen
[[nodiscard]] std::pair<float, float> legalScreenFactors(const LegalScreenSettings& settings);

/// Where the legal screen's picture goes on the logical screen: a sprite of size (fx × d × (u1 - u0), fy × d ×
/// (v1 - v0)) at (0, 0, -d) in front of the overlay camera `camera`, `(fx, fy)` the factors and `rect` the page's first
/// rectangle, centred on the screen. In the default mode it covers 1.068 × 1.011 of the screen with the disc's picture:
/// slightly more than all of it. `d` cancels out.
///
/// Research: docs/research/graphics.md#first-screen
[[nodiscard]] graphics::LogicalQuad legalScreenQuad(const graphics::OverlayCamera& camera,
                                                    std::pair<float, float> factors, const graphics::UvRect& rect);

/// Game mode 5, the legal screen: the first screen after the start-up movies. Its entry runs the preload scripts
/// (`enum_preload.lua`, then `config_preload.lua`, `config_preload2.lua` and `config_preload3.lua`, which set up the
/// enumerations, the UI strings, the configuration and the level table). It then loads the legal screen's sprite
/// sheet, shows its first rectangle on black, centred and slightly overfilling the screen as the original sizes it
/// (legalScreenQuad()), holds it for 5,000 ms of game time whatever the player presses, then leaves.
///
/// Coney's differences from the original, all invisible on the screen:
/// - The original draws the picture once in `Enter`, into both display buffers, and its `Update` presents nothing;
///   Coney's render() draws and presents the same picture every frame, so a window that is moved, resized or
///   captured still shows it.
/// - The original times the hold in real milliseconds; Coney counts game time on the fixed 1/30 s step, so the hold is
///   exactly 150 frames and a test can run it without a clock.
/// - The preloads run before the picture loads; the original runs them in `Enter` too (the order within `Enter` is
///   not on the page). Coney's reads are synchronous, so they take no frames.
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

    /// Draws through `device` with the sheet `loadSheet` returns for `settings`' resource, and runs the preloads in
    /// `scripts` (null: none). A failed load is passed to `log` and the screen stays black for the hold. `device` and
    /// `scripts` must outlive the mode.
    LegalScreenMode(graphics::RenderDevice& device, SheetLoader loadSheet, LegalScreenSettings settings,
                    std::function<void(std::string_view)> log, script::ScriptSystem* scripts = nullptr);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Runs the preload scripts, loads the picture and starts the hold.
    /// @orig 0x00159a58 Mode5::Enter (unknown)
    /// @orig 0x00161218 RunPreloadScripts (unknown)
    /// @orig 0x00159c08 StartupScreen_Draw (unknown)
    void enter() override;

    /// Leaves once kHoldMilliseconds of game time have passed since the mode was entered. Input is not read: no
    /// button skips the screen.
    /// @orig 0x00159ae0 Mode5::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Clears the logical screen to black and draws the picture over it, then presents. Nothing moves, so there is
    /// nothing to blend.
    void render(const RenderTime& time) override;

    /// Releases the picture.
    /// @orig 0x00159ab8 Mode5::Exit (unknown)
    void exit() override;

    /// The sheet shown, while the mode is entered and the load succeeded.
    [[nodiscard]] const std::optional<graphics::SpriteSheet>& sheet() const { return m_sheet; }

  private:
    graphics::RenderDevice& m_device;

    SheetLoader m_loadSheet;
    LegalScreenSettings m_settings;
    std::function<void(std::string_view)> m_log;
    script::ScriptSystem* m_scripts; // runs the preloads; not owned, may be null
    std::optional<graphics::SpriteSheet> m_sheet;
    std::optional<std::uint64_t> m_startTicks; // game time when the mode was entered; set by the first update
};

} // namespace coney
