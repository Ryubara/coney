// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "core/chunk_system.h"
#include "effects/room_smoke.h"
#include "fileio/wad.h"
#include "graphics/particle_page.h"
#include "platform/render_engine.h"

namespace coney::platform {

/// The room smoke's quad for `sprite`, in logical pixels that stand for the whole picture the 3D view fills
/// (RenderEngine::drawWrappedViewQuads()): projected through the 16:9 mode's overlay camera when `widescreen` (scale
/// 1.1, aspect 1.6667: the picture 2.017 overlay units wide and 1.21 high at the GUI depth), else the 4:3 mode's
/// (1.595 x 1.1). The sprite's size, position and texture rectangle are the same in both modes; only the camera
/// changes (docs/research/graphics.md#room-smoke).
/// @orig 0x0019b4c0 OE_RoomSmoke_Update (OE_RoomSmoke.cpp)
[[nodiscard]] graphics::LogicalQuad roomSmokeQuad(const effects::SmokeSprite& sprite, bool widescreen);

/// Draws the room-smoke overlay (effects::RoomSmoke): its sprite of the `room_smoke_overlay` sheet over the 3D frame,
/// before the HUD, blended by its alpha with the texture repeating, across the whole picture with the overlay camera of
/// the window's mode (roomSmokeQuad(), docs/research/graphics.md#room-smoke). The sheet is
/// loaded the first time the smoke runs; one that does not load is reported once through `print` and nothing is
/// drawn.
class RoomSmokeOverlay {
  public:
    /// Loads the sheet from `wad` (which must outlive this) when first needed.
    RoomSmokeOverlay(const io::Wad& wad, std::function<void(std::string_view)> print);

    /// Draws `smoke` while it runs, on an engine that draws pixels.
    void draw(RenderEngine& engine, const effects::RoomSmoke& smoke);

  private:
    const io::Wad& m_wad;
    std::function<void(std::string_view)> m_print;
    chunk::ChunkHandlerTable m_table;
    std::optional<graphics::SpriteSheet> m_sheet;
    bool m_tried = false; // the sheet's load was tried
};

} // namespace coney::platform
