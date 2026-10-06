// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"
#include "platform/texture_dictionary.h"

// librw's texture type, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Texture;
} // namespace rw

namespace coney::platform {

/// A sprite sheet's texture: the one texture of the dictionary read just before the sheet's 0x4C chunk. Owns that
/// dictionary, so the texture lives as long as this object; like every librw object it must be destroyed before the
/// RenderEngine stops.
class SheetTexture final : public graphics::Texture {
  public:
    /// Takes `dictionary`, whose first texture becomes the sheet's. Fails with ErrorCode::Invalid when the dictionary
    /// holds no texture.
    [[nodiscard]] static std::expected<std::unique_ptr<SheetTexture>, Error> create(TextureDictionary dictionary);

    [[nodiscard]] int width() const override;
    [[nodiscard]] int height() const override;

    /// How many textures the dictionary holds; the sheet uses the first. The game's sheets all have exactly one.
    [[nodiscard]] std::size_t dictionaryTextureCount() const { return m_dictionary.textures().size(); }

    /// The librw texture, for the renderer.
    [[nodiscard]] rw::Texture* rwTexture() const { return m_texture; }

    /// The texture as RGBA pixels, as TextureDictionary::toImages() gives it; on either backend, before
    /// convertForDrawing(). Fails as toImages() does.
    [[nodiscard]] std::expected<RgbaImage, Error> toImage() const;

    /// Converts the texture for drawing, as TextureDictionary::convertForDrawing() does (OpenGL backend only).
    [[nodiscard]] std::expected<void, Error> convertForDrawing() { return m_dictionary.convertForDrawing(); }

  private:
    SheetTexture(TextureDictionary dictionary, rw::Texture* texture)
        : m_dictionary(std::move(dictionary)), m_texture(texture) {}

    TextureDictionary m_dictionary; // owns m_texture
    rw::Texture* m_texture;
};

/// A sprite sheet on the chunk stack, pushed by the 0x4C handler under type 0x4C: the page's rectangles bound to the
/// texture of the dictionary before it.
class SpriteSheetObject final : public chunk::LoadedObject {
  public:
    SpriteSheetObject(graphics::ParticlePage page, std::shared_ptr<SheetTexture> texture)
        : m_page(std::move(page)), m_texture(std::move(texture)) {}

    [[nodiscard]] std::string_view describe() const override { return "sprite sheet"; }

    /// The rectangles.
    [[nodiscard]] const graphics::ParticlePage& page() const { return m_page; }
    /// The texture; never null.
    [[nodiscard]] const std::shared_ptr<SheetTexture>& texture() const { return m_texture; }

    /// The sheet as game code uses it: the rectangles and a shared reference to the texture.
    [[nodiscard]] graphics::SpriteSheet sheet() const { return graphics::SpriteSheet{m_page, m_texture}; }

  private:
    graphics::ParticlePage m_page;
    std::shared_ptr<SheetTexture> m_texture;
};

/// Chunk type 0x4C, "Particle Page": a sprite sheet's rectangles.
inline constexpr std::uint32_t kParticlePage = 0x4C;

/// The onLoaded handler of chunk type 0x4C: pops the 0x4C chunk and the texture dictionary (0x0B) read before it,
/// parses the page (graphics::parseParticlePage()) and pushes a SpriteSheetObject back under 0x4C. The original
/// links the page to the dictionary in place; Coney builds an object that owns both. Fails as ChunkStacks::popChunk()
/// and graphics::parseParticlePage() do, and with ErrorCode::Invalid when the 0x0B chunk holds no texture dictionary
/// object or the dictionary holds no texture.
///
/// Research: docs/research/gui.md#particle-page, docs/research/chunk-system.md#chunk-type-table
/// @orig 0x00181b20 ChunkLoaded_ParticlePage (unknown)
[[nodiscard]] std::expected<void, Error> onParticlePageLoaded(chunk::ChunkStacks& stacks, std::uint32_t type);

/// Registers onParticlePageLoaded() for type 0x4C and graphics::onParticlePageHeaderLoaded() for type 0x4D (the
/// sheet table) in `table`. The 0x4C handler needs the texture dictionary readers
/// of addTextureDictionaryHandlers() in the same table.
void addSpriteSheetHandlers(chunk::ChunkHandlerTable& table);

/// Loads the sprite sheets of one WAD entry with `table` (which needs the handlers of addTextureDictionaryHandlers()
/// and addSpriteSheetHandlers()), in the order the entry holds them. Fails with ErrorCode::NotFound when the entry
/// holds none, and as the load does otherwise.
[[nodiscard]] std::expected<std::vector<std::unique_ptr<SpriteSheetObject>>, Error>
loadSpriteSheets(const io::Wad& wad, const io::WadEntry& entry, const chunk::ChunkHandlerTable& table);

/// Loads the first sprite sheet of `entry` and with `convertForDrawing` converts its texture for the OpenGL renderer.
/// Fails as loadSpriteSheets() and the conversion do.
[[nodiscard]] std::expected<graphics::SpriteSheet, Error> loadSpriteSheet(const io::Wad& wad, const io::WadEntry& entry,
                                                                          const chunk::ChunkHandlerTable& table,
                                                                          bool convertForDrawing);

/// Loads the sprite sheet resource named `resourceName` (such as `legal_screen`) from the WAD file named by its
/// decimal CRC-32 (coney::resourceFileName()), and with `convertForDrawing` converts its texture for the OpenGL
/// renderer. Returns the first sheet of the entry. Fails with ErrorCode::NotFound when no such entry or no sheet in it
/// exists, and as the load or the conversion does otherwise.
[[nodiscard]] std::expected<graphics::SpriteSheet, Error> loadSpriteSheetResource(const io::Wad& wad,
                                                                                  const chunk::ChunkHandlerTable& table,
                                                                                  std::string_view resourceName,
                                                                                  bool convertForDrawing);

} // namespace coney::platform
