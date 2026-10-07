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

/// Draws the room-smoke overlay (effects::RoomSmoke): its sprite of the `room_smoke_overlay` sheet over the 3D frame,
/// before the HUD, blended by its alpha with the texture repeating (docs/research/graphics.md#room-smoke). The sheet is
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
