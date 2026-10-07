// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/room_smoke_overlay.h"

#include <array>
#include <format>
#include <utility>

#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

namespace {

// The sheet's resource name (sheet-table record 19, sprite word `0x130000`).
constexpr std::string_view kSheet = "room_smoke_overlay";
// The texture rectangle starts a tenth above the texture's top.
constexpr float kTopV = -0.1F;

} // namespace

RoomSmokeOverlay::RoomSmokeOverlay(const io::Wad& wad, std::function<void(std::string_view)> print)
    : m_wad(wad), m_print(std::move(print)), m_table(chunk::ChunkHandlerTable::withDefaults()) {
    addTextureDictionaryHandlers(m_table);
    addSpriteSheetHandlers(m_table);
}

void RoomSmokeOverlay::draw(RenderEngine& engine, const effects::RoomSmoke& smoke) {
    if (!smoke.running() || !engine.drawsPixels()) {
        return;
    }
    if (!m_tried) {
        m_tried = true;
        auto loaded = loadSpriteSheetResource(m_wad, m_table, kSheet, true);
        if (loaded) {
            m_sheet = std::move(*loaded);
        } else if (m_print) {
            m_print(std::format("room smoke: sheet {} not loaded: {}\n", kSheet, loaded.error().message));
        }
    }
    if (!m_sheet) {
        return;
    }
    // The sprite centred across at its GUI height, sized in overlay units at the GUI depth.
    const effects::SmokeSprite& sprite = smoke.sprite();
    const graphics::OverlayCamera camera;
    const graphics::LogicalPoint centre = camera.guiToLogical(0.5F, sprite.guiY);
    const graphics::LogicalPoint size =
        camera.projectSize(sprite.width, sprite.height, graphics::OverlayCamera::kGuiDepth);
    const std::array<graphics::LogicalQuad, 1> quad{graphics::LogicalQuad{
        .x = centre.x - (size.x * 0.5F),
        .y = centre.y - (size.y * 0.5F),
        .width = size.x,
        .height = size.y,
        .uv = graphics::UvRect{.u0 = sprite.u, .v0 = kTopV, .u1 = sprite.u + 1.0F, .v1 = kTopV + 1.0F},
        .colour = graphics::Rgba{sprite.colour[0], sprite.colour[1], sprite.colour[2], sprite.colour[3]}}};
    engine.drawWrappedQuads(m_sheet->texture.get(), quad);
}

} // namespace coney::platform
