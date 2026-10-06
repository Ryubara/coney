// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sprite_sheets.h"

#include <format>
#include <string>
#include <utility>

#include <rw.h>

#include "core/chunk_types.h"
#include "gamemodes/legal_screen_mode.h"
#include "gamemodes/load_entry_mode.h"

namespace coney::platform {

std::expected<std::unique_ptr<SheetTexture>, Error> SheetTexture::create(TextureDictionary dictionary) {
    const std::vector<rw::Texture*> textures = dictionary.textures();
    if (textures.empty()) {
        return fail(ErrorCode::Invalid, "a sprite sheet's texture dictionary holds no texture");
    }
    // The sheet's texture is the dictionary's first (in the game's data, its only) texture.
    rw::Texture* first = textures.front();
    return std::unique_ptr<SheetTexture>(new SheetTexture(std::move(dictionary), first));
}

int SheetTexture::width() const { return m_texture->raster != nullptr ? m_texture->raster->width : 0; }

int SheetTexture::height() const { return m_texture->raster != nullptr ? m_texture->raster->height : 0; }

std::expected<RgbaImage, Error> SheetTexture::toImage() const {
    auto images = m_dictionary.toImages();
    if (!images) {
        return std::unexpected(std::move(images.error()));
    }
    // create() refused a dictionary without a texture, and the sheet's is the first.
    return std::move(images->front());
}

std::expected<void, Error> onParticlePageLoaded(chunk::ChunkStacks& stacks, std::uint32_t type) {
    // The page first, then the dictionary written before it, as the original pops them.
    auto pageChunk = stacks.popChunk(type);
    if (!pageChunk) {
        return std::unexpected(std::move(pageChunk.error()));
    }
    auto page = graphics::parseParticlePage(pageChunk->bytes);
    if (!page) {
        stacks.pushChunk(std::move(*pageChunk)); // leave the stack as it was
        return std::unexpected(std::move(page.error()));
    }
    auto dictionaryChunk = stacks.popChunk(chunk::kTextureDictionaryTid);
    if (!dictionaryChunk) {
        stacks.pushChunk(std::move(*pageChunk));
        return std::unexpected(std::move(dictionaryChunk.error()));
    }
    auto* dictionary = dynamic_cast<TextureDictionaryObject*>(dictionaryChunk->object.get());
    if (dictionary == nullptr) {
        return fail(ErrorCode::Invalid, "the texture dictionary chunk before a particle page holds no dictionary");
    }
    auto texture = SheetTexture::create(std::move(dictionary->dictionary()));
    if (!texture) {
        return std::unexpected(std::move(texture.error()));
    }
    chunk::ChunkData result;
    result.type = kParticlePage;
    result.id = pageChunk->id;
    result.object =
        std::make_unique<SpriteSheetObject>(std::move(*page), std::shared_ptr<SheetTexture>(std::move(*texture)));
    stacks.pushChunk(std::move(result));
    return {};
}

void addSpriteSheetHandlers(chunk::ChunkHandlerTable& table) {
    table.setHandlers(kParticlePage, chunk::ChunkHandlers{onParticlePageLoaded, {}});
    table.setHandlers(graphics::kParticlePageHeader, chunk::ChunkHandlers{graphics::onParticlePageHeaderLoaded, {}});
}

std::expected<std::vector<std::unique_ptr<SpriteSheetObject>>, Error>
loadSpriteSheets(const io::Wad& wad, const io::WadEntry& entry, const chunk::ChunkHandlerTable& table) {
    auto load = loadWadEntry(wad, entry, table);
    if (!load) {
        return std::unexpected(std::move(load.error()));
    }
    std::vector<chunk::ChunkData> chunks = load->stacks.takeChunks(kParticlePage);
    std::vector<std::unique_ptr<SpriteSheetObject>> sheets;
    sheets.reserve(chunks.size());
    for (chunk::ChunkData& data : chunks) {
        if (dynamic_cast<SpriteSheetObject*>(data.object.get()) != nullptr) {
            sheets.emplace_back(static_cast<SpriteSheetObject*>(data.object.release()));
        }
    }
    if (sheets.empty()) {
        return fail(ErrorCode::NotFound, "the entry holds no sprite sheet");
    }
    return sheets;
}

std::expected<graphics::SpriteSheet, Error> loadSpriteSheetResource(const io::Wad& wad,
                                                                    const chunk::ChunkHandlerTable& table,
                                                                    std::string_view resourceName,
                                                                    bool convertForDrawing) {
    const std::string fileName = resourceFileName(resourceName);
    auto entry = wad.lookup(fileName);
    if (!entry) {
        return std::unexpected(
            Error{entry.error().code, std::format("{} ({}): {}", resourceName, fileName, entry.error().message)});
    }
    return loadSpriteSheet(wad, **entry, table, convertForDrawing);
}

std::expected<graphics::SpriteSheet, Error> loadSpriteSheet(const io::Wad& wad, const io::WadEntry& entry,
                                                            const chunk::ChunkHandlerTable& table,
                                                            bool convertForDrawing) {
    auto sheets = loadSpriteSheets(wad, entry, table);
    if (!sheets) {
        return std::unexpected(std::move(sheets.error()));
    }
    const SpriteSheetObject& sheet = *sheets->front();
    if (convertForDrawing) {
        if (auto converted = sheet.texture()->convertForDrawing(); !converted) {
            return std::unexpected(std::move(converted.error()));
        }
    }
    return sheet.sheet();
}

} // namespace coney::platform
