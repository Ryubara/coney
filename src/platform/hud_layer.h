// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "core/chunk_system.h"
#include "fileio/wad.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "hud/hud.h"

namespace coney::platform {

/// The name hash of every record of the sprite-sheet table in `warriors.glr` (chunk 0x4D,
/// docs/research/gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header), by record; none when it does not load.
/// A record's sheet is the WAD file named by its hash in decimal.
[[nodiscard]] std::vector<std::uint32_t> sheetTableHashes(const io::Wad& wad);

/// The HUD on screen: the sprite sheets and batches the HUD draws with, loaded from the disc, and the 2D pass that
/// draws them over the play mode's frame. It drives a hud::Hud (its own, or one the game shares with the scripts'
/// bindings, useHud()): step() is the HUD's step and lists its sprites, draw() draws the newest step's sprites, as
/// often as frames ask.
///
/// The sheets: `part_page0` (text font slot 2 and the HUD's icons), `big_font` (font slot 6, and Coney's stand-in for
/// the radar disc), `hud_minigames` (the arrow), and each banner's sheet by its sheet-table record. A sheet that does
/// not load leaves out what it would draw, with a line in the log.
///
/// Research: docs/research/hud.md, docs/research/gui.md#a-frame-of-2d
class HudLayer {
  public:
    /// Loads the HUD's sheets from `wad`, converted for drawing with `drawsPixels`. `print` gets a line per sheet that
    /// fails. Never fails itself: a HUD without sheets draws nothing.
    [[nodiscard]] static std::unique_ptr<HudLayer> create(const io::Wad& wad, bool drawsPixels,
                                                          const std::function<void(std::string_view)>& print);

    /// The HUD stepped and drawn: the layer's own until useHud().
    [[nodiscard]] hud::Hud& hud() { return *m_hud; }
    /// Steps and draws `shared` (which must outlive the layer) instead of the layer's own HUD: the game's, which the
    /// scripts' bindings act on.
    void useHud(hud::Hud& shared) { m_hud = &shared; }

    /// The HUD's step with `frame`, then its sprites for the step: the pass emptied, the HUD rendered into the batches,
    /// the batches queued. Loads a banner's sheet the first time a panel shows it.
    void step(const hud::HudFrame& frame);
    /// Draws the newest step's sprites through the overlay camera; changes nothing step() reads.
    void draw(graphics::RenderDevice& device);
    /// How many sprites the newest step queued (0 while the HUD is hidden or letterboxed), for the tests and the log.
    [[nodiscard]] std::size_t spritesQueued() const { return m_spritesQueued; }

  private:
    HudLayer(const io::Wad& wad, bool drawsPixels);

    // Loads sprite sheet `name` (a resource name), or nothing with a log line.
    [[nodiscard]] std::optional<graphics::SpriteSheet> loadSheet(std::string_view name,
                                                                 const std::function<void(std::string_view)>& print);
    // The batch pair (shadow, banner) of banner sheet `record`, loaded the first time; nothing when it fails.
    struct BannerBatches {
        std::unique_ptr<graphics::SpriteBatch> shadow;
        std::unique_ptr<graphics::SpriteBatch> banner;
    };
    BannerBatches* bannerBatches(std::uint32_t record);

    const io::Wad& m_wad;
    bool m_drawsPixels;
    chunk::ChunkHandlerTable m_handlers;
    std::vector<std::uint32_t> m_sheetHashes; // the sheet table's name hashes by record
    hud::Hud m_ownHud;
    hud::Hud* m_hud = &m_ownHud;
    std::optional<graphics::Font> m_textFont; // part_page0, slot 2
    std::optional<graphics::Font> m_bigFont;  // big_font, slot 6
    std::unique_ptr<graphics::SpriteBatch> m_parts;
    std::unique_ptr<graphics::SpriteBatch> m_bigText;
    std::unique_ptr<graphics::SpriteBatch> m_radar;
    std::unique_ptr<graphics::SpriteBatch> m_minigames;
    std::unique_ptr<graphics::SpriteBatch> m_flat;
    std::map<std::uint32_t, BannerBatches> m_banners; // by record; empty batches for a sheet that failed
    graphics::OverlayCamera m_camera;
    graphics::OverlayPass m_pass;
    std::size_t m_spritesQueued = 0; // spritesQueued()
};

} // namespace coney::platform
