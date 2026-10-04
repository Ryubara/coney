// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "graphics/texture_grid.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

/// A game mode that shows every texture of some texture dictionaries, laid out in a grid that fills the window
/// (graphics::layoutGrid()) and drawn with their alpha. It backs `coney --view-txd`
/// (docs/guides/building.md#run-coney). It stays until the window closes or the frame limit is hit; it reads no clock,
/// so every frame of a run is the same.
///
/// Coney's own tool; nothing in the original corresponds.
class TextureViewerMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range of ids.
    static constexpr std::uint32_t kId = 0x102;
    /// The background: a neutral mid grey, against which both dark and light texels and their transparency show.
    static constexpr graphics::Rgba kClearColour{72, 72, 80, 255};
    /// Pixels around and between the grid's cells.
    static constexpr int kMargin = 8;

    /// Shows the textures of `dictionaries`, in order. With the OpenGL backend they must have been converted with
    /// TextureDictionary::convertForDrawing(); with NULL nothing is drawn. `engine` must outlive the mode.
    TextureViewerMode(RenderEngine& engine, std::vector<TextureDictionary> dictionaries);

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// The number of textures shown.
    [[nodiscard]] std::size_t textureCount() const { return m_textures.size(); }

  private:
    RenderEngine& m_engine;
    std::vector<TextureDictionary> m_dictionaries; // owns the textures in m_textures
    std::vector<rw::Texture*> m_textures;
    std::vector<graphics::Extent> m_sizes; // of each texture, for the layout
};

} // namespace coney::platform
