// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/hud_layer.h"

#include <format>
#include <string>
#include <utility>

#include "gamemodes/load_entry_mode.h"
#include "graphics/particle_page.h"
#include "gui/text_layout.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

namespace {

// The batches' capacities and depths (the 2D sort keys, docs/research/gui.md#draw-order): the flat quads under
// everything, the radar disc, the big font, then part_page0 (the text and the icons) and hud_minigames.
constexpr std::size_t kTextCapacity = 2048;
constexpr std::size_t kSmallCapacity = 64;
constexpr float kFlatDepth = 8000.0F;
constexpr float kRadarDepth = 8500.0F;
constexpr float kBigTextDepth = 9000.0F;
constexpr float kPartsDepth = 10000.0F;
constexpr float kMinigamesDepth = 10000.0F;

// The sprite sheet table of warriors.glr: the name hash of every record, or none when it does not load.
std::vector<std::uint32_t> sheetTableHashes(const io::Wad& wad) {
    std::vector<std::uint32_t> hashes;
    auto entry = wad.lookup("warriors.glr");
    if (!entry) {
        return hashes;
    }
    // The table is one raw chunk among the global resource's lists; no handler is needed to find it.
    const chunk::ChunkHandlerTable none;
    auto load = loadWadEntry(wad, **entry, none);
    if (!load) {
        return hashes;
    }
    std::vector<chunk::ChunkData> chunks = load->stacks.takeChunks(graphics::kParticlePageHeader);
    if (chunks.empty()) {
        return hashes;
    }
    if (auto table = graphics::parseSpriteSheetTable(chunks.front().bytes)) {
        for (const graphics::SheetTableRecord& record : table->records) {
            hashes.push_back(record.nameHash);
        }
    }
    return hashes;
}

} // namespace

HudLayer::HudLayer(const io::Wad& wad, bool drawsPixels)
    : m_wad(wad), m_drawsPixels(drawsPixels), m_handlers(chunk::ChunkHandlerTable::withDefaults()) {
    addTextureDictionaryHandlers(m_handlers);
    addSpriteSheetHandlers(m_handlers);
}

std::unique_ptr<HudLayer> HudLayer::create(const io::Wad& wad, bool drawsPixels,
                                           const std::function<void(std::string_view)>& print) {
    std::unique_ptr<HudLayer> layer(new HudLayer(wad, drawsPixels));
    layer->m_sheetHashes = sheetTableHashes(wad);
    // part_page0: the text font of slot 2 and the HUD's icons, one batch as the original's one instance.
    if (auto sheet = layer->loadSheet(gui::kTextFontSheet, print)) {
        layer->m_parts = std::make_unique<graphics::SpriteBatch>(*sheet, kTextCapacity, kPartsDepth);
        if (auto font = graphics::Font::fromSheet(std::move(*sheet))) {
            layer->m_textFont = std::move(*font);
        }
    }
    // big_font: slot 6, and the radar disc's stand-in.
    if (auto sheet = layer->loadSheet(gui::kBigFontSheet, print)) {
        layer->m_bigText = std::make_unique<graphics::SpriteBatch>(*sheet, kTextCapacity, kBigTextDepth);
        layer->m_radar = std::make_unique<graphics::SpriteBatch>(*sheet, kSmallCapacity, kRadarDepth);
        if (auto font = graphics::Font::fromSheet(std::move(*sheet))) {
            layer->m_bigFont = std::move(*font);
        }
    }
    if (auto sheet = layer->loadSheet("hud_minigames", print)) {
        layer->m_minigames =
            std::make_unique<graphics::SpriteBatch>(std::move(*sheet), kSmallCapacity, kMinigamesDepth);
    }
    // Untextured quads: a sheet with no texture draws flat colour.
    layer->m_flat = std::make_unique<graphics::SpriteBatch>(graphics::SpriteSheet{}, kSmallCapacity, kFlatDepth);
    if (layer->m_sheetHashes.empty()) {
        print("hud: no sprite sheet table in warriors.glr; no name banners\n");
    }
    return layer;
}

std::optional<graphics::SpriteSheet> HudLayer::loadSheet(std::string_view name,
                                                         const std::function<void(std::string_view)>& print) {
    auto sheet = loadSpriteSheetResource(m_wad, m_handlers, name, m_drawsPixels);
    if (!sheet) {
        print(std::format("hud: sprite sheet {}: {}\n", name, sheet.error().message));
        return std::nullopt;
    }
    return std::move(*sheet);
}

HudLayer::BannerBatches* HudLayer::bannerBatches(std::uint32_t record) {
    auto found = m_banners.find(record);
    if (found == m_banners.end()) {
        // The first use: load the record's sheet from the WAD file named by its name hash; a failure is kept as empty
        // batches so it is tried once.
        BannerBatches batches;
        if (record < m_sheetHashes.size()) {
            if (auto entry = m_wad.lookup(std::to_string(m_sheetHashes[record]))) {
                if (auto sheet = loadSpriteSheet(m_wad, **entry, m_handlers, m_drawsPixels)) {
                    batches.shadow = std::make_unique<graphics::SpriteBatch>(*sheet, 4, hud::kBannerShadowDepth);
                    batches.banner = std::make_unique<graphics::SpriteBatch>(std::move(*sheet), 2, hud::kBannerDepth);
                }
            }
        }
        found = m_banners.emplace(record, std::move(batches)).first;
    }
    return &found->second;
}

void HudLayer::step(const hud::HudFrame& frame) {
    m_hud->update(frame);
    m_pass.empty();
    // The canvas: fonts by slot (slot 6 big_font, every other slot part_page0) and their batches.
    hud::HudCanvas canvas;
    canvas.text.fonts = [this](int slot) -> const graphics::Font* {
        if (slot == gui::kBigFontSlot && m_bigFont) {
            return &*m_bigFont;
        }
        return m_textFont ? &*m_textFont : nullptr;
    };
    canvas.text.textBatch = [this](int slot) -> graphics::SpriteBatch* {
        return slot == gui::kBigFontSlot && m_bigText ? m_bigText.get() : m_parts.get();
    };
    canvas.parts = m_parts.get();
    canvas.minigames = m_minigames.get();
    canvas.flat = m_flat.get();
    canvas.radar = m_radar.get();
    canvas.banner = [this](std::uint32_t record, bool shadow) -> graphics::SpriteBatch* {
        BannerBatches* batches = bannerBatches(record);
        return shadow ? batches->shadow.get() : batches->banner.get();
    };
    m_hud->render(canvas);
    // Every batch is queued; an empty one draws nothing.
    for (graphics::SpriteBatch* batch :
         {m_flat.get(), m_radar.get(), m_bigText.get(), m_parts.get(), m_minigames.get()}) {
        if (batch != nullptr) {
            m_pass.queue(*batch);
        }
    }
    for (auto& [record, batches] : m_banners) {
        if (batches.shadow) {
            m_pass.queue(*batches.shadow);
            m_pass.queue(*batches.banner);
        }
    }
}

void HudLayer::draw(graphics::RenderDevice& device) { m_pass.draw(device, m_camera); }

} // namespace coney::platform
