// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/texture_dictionary.h"

#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <utility>

#include <rw.h>

#include "core/assert.h"
#include "core/chunk_types.h"
#include "gamemodes/load_entry_mode.h"

namespace coney::platform {

namespace {

// The error for a texture librw would not convert.
std::unexpected<Error> conversionFailure(const rw::Texture* texture, std::string_view what) {
    return std::unexpected(
        Error{ErrorCode::Invalid, std::format("librw could not convert texture \"{}\" {}", texture->name, what)});
}

// Reads a whole stream into memory from its start.
std::expected<std::vector<std::byte>, Error> readAll(io::Stream& stream) {
    if (auto moved = stream.seek(0); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.size()));
    if (auto read = stream.read(bytes); !read) {
        return std::unexpected(std::move(read.error()));
    }
    return bytes;
}

// The body both texture chunk readers share: find the dictionary section in the chunk, read it and push it under
// type 0x0B. The original's 0x0B reader finds the section with RenderWare's stream functions the same way
// (docs/research/chunk-system.md, after the chunk type table).
std::expected<void, Error> readDictionaryIntoStacks(io::Stream& chunk, const chunk::ChunkHeader& header,
                                                    chunk::ChunkStacks& stacks) {
    auto bytes = readAll(chunk);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    auto start = graphics::findRwSection(*bytes, graphics::kRwTexDictionary);
    if (!start) {
        return std::unexpected(std::move(start.error()));
    }
    auto dictionary = TextureDictionary::read(std::span<const std::byte>(*bytes).subspan(*start));
    if (!dictionary) {
        return std::unexpected(std::move(dictionary.error()));
    }
    chunk::ChunkData result;
    result.type = chunk::kTextureDictionaryTid;
    result.id = header.id;
    result.object = std::make_unique<TextureDictionaryObject>(std::move(*dictionary));
    stacks.pushChunk(std::move(result));
    return {};
}

// Limits a converted texture to the `levels` mip levels its PS2 raster had. librw's GL3 raster of a mipmapped format
// allocates the whole chain down to 1 x 1 but the conversion fills only the levels the source has; with the rest
// empty, OpenGL treats the texture as incomplete under a mipmap filter and samples black (most of the world's
// mipmapped textures have one level, MXL 0, docs/research/rendering.md#world). With one level the texture is
// filtered without mipmaps, as on the GS.
void keepLevels(rw::Raster* raster, rw::int32 levels) {
    auto* native = PLUGINOFFSET(rw::gl3::Gl3Raster, raster, rw::gl3::nativeRasterOffset);
    if (raster->platform != rw::PLATFORM_GL3 || native->autogenMipmap || levels < 1 || native->numLevels <= levels) {
        return;
    }
    native->numLevels = static_cast<rw::int8>(levels);
    const rw::uint32 bound = rw::gl3::bindTexture(native->texid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    rw::gl3::bindTexture(bound); // librw's state cache still holds the texture bound before
    native->filterMode = 0xFF;   // the device sets the filter again, without mipmaps for a single level
}

// Whether librw's PS2 texture reader can take `texture` without writing past the buffers it allocates. librw sizes
// a raster's buffers by its own reckoning of the GS layout and then copies as many bytes as the stream's header says;
// for some of the game's smallest textures (a 2 x 2 4-bit texture padded to the GS's minimum transfer, for one) the
// stream's sizes are the larger, and the copy would overrun the heap.
bool librwCanRead(const graphics::Ps2TextureInfo& texture) {
    if (texture.version < 2) {
        // The older layout: librw allocates plain rows of texels plus the palette and copies the whole pixel section
        // into them. Mipmaps make that allocation harder to predict, so they are not accepted here.
        if ((texture.rasterFormat & 0x8000U) != 0) {
            return false;
        }
        const std::uint32_t paletteColour = (texture.rasterFormat & 0x0F00U) == 0x0100U ? 2 : 4;
        std::uint32_t palette = 0;
        if ((texture.rasterFormat & 0x2000U) != 0) {
            palette = 16 * 16 * paletteColour;
        } else if ((texture.rasterFormat & 0x4000U) != 0) {
            palette = 8 * 2 * paletteColour;
        }
        const std::uint64_t rows = std::uint64_t{texture.width} * texture.depth / 8 * texture.height;
        return std::uint64_t{texture.pixelBytes} + texture.paletteBytes == (rows + 15) / 16 * 16 + palette;
    }
    // The newer layout: ask librw what it allocates, by making a raster of the same shape as its reader would (with
    // the library version of the raster's header, which decides whether 4-bit texels are swizzled).
    const rw::int32 savedVersion = rw::version;
    rw::version = static_cast<rw::int32>(rw::libraryIDUnpackVersion(texture.rasterLibraryStamp));
    rw::Raster* raster = rw::Raster::create(
        static_cast<rw::int32>(texture.width), static_cast<rw::int32>(texture.height),
        static_cast<rw::int32>(texture.depth), static_cast<rw::int32>(texture.rasterFormat), rw::PLATFORM_PS2);
    rw::version = savedVersion;
    if (raster == nullptr) {
        return false;
    }
    const rw::ps2::Ps2Raster* layout = GETPS2RASTEREXT(raster);
    const bool fits = layout->pixelSize >= texture.pixelBytes && layout->paletteSize >= texture.paletteBytes;
    raster->destroy();
    return fits;
}

// Copies the dictionary at the start of `stream` for librw, leaving out the textures librwCanRead() refuses: their
// sections are dropped, the count and the dictionary's size are patched. Moves the kept textures' descriptions into
// `info.textures` and returns how many were left out.
std::size_t copyReadableTextures(std::span<const std::byte> stream, graphics::TexDictionaryInfo& info,
                                 std::vector<rw::uint8>& copy) {
    std::vector<graphics::Ps2TextureInfo> kept;
    std::vector<std::span<const std::byte>> sections;
    for (graphics::Ps2TextureInfo& texture : info.textures) {
        if (librwCanRead(texture)) {
            sections.push_back(stream.subspan(texture.sectionOffset, texture.sectionBytes));
            kept.push_back(std::move(texture));
        }
    }
    const std::size_t skipped = info.textures.size() - kept.size();
    const auto append = [&copy](std::span<const std::byte> bytes) {
        for (const std::byte b : bytes) {
            copy.push_back(static_cast<rw::uint8>(b));
        }
    };
    if (skipped == 0 || info.textures.empty()) {
        append(stream.first(info.streamBytes));
    } else {
        // Everything before the first texture (the dictionary's header and struct), the kept textures, then
        // everything after the last texture (the dictionary's extension).
        const graphics::Ps2TextureInfo& last = info.textures.back();
        const std::size_t texturesEnd = last.sectionOffset + last.sectionBytes;
        append(stream.first(info.textures.front().sectionOffset));
        for (const std::span<const std::byte> section : sections) {
            append(section);
        }
        append(stream.subspan(texturesEnd, info.streamBytes - texturesEnd));
        // The count is the struct's first 16 bits, after the two 12-byte section headers; then the dictionary size.
        copy[24] = static_cast<rw::uint8>(kept.size() & 0xFFU);
        copy[25] = static_cast<rw::uint8>(kept.size() >> 8);
        const auto size = static_cast<std::uint32_t>(copy.size() - graphics::kRwHeaderSize);
        for (std::size_t i = 0; i < 4; ++i) {
            copy[4 + i] = static_cast<rw::uint8>((size >> (8 * i)) & 0xFFU);
        }
    }
    info.textures = std::move(kept);
    return skipped;
}

} // namespace

std::expected<TextureDictionary, Error> TextureDictionary::read(std::span<const std::byte> stream) {
    auto info = graphics::inspectTexDictionary(stream);
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    // librw reads from a buffer it may write to, so give it a copy of exactly the dictionary, without the textures it
    // cannot read safely.
    std::vector<rw::uint8> copy;
    const std::size_t skipped = copyReadableTextures(stream, *info, copy);
    const auto length = static_cast<rw::uint32>(copy.size());
    rw::StreamMemory memory;
    memory.open(copy.data(), length, length);
    rw::TexDictionary* dictionary = nullptr;
    if (rw::findChunk(&memory, rw::ID_TEXDICTIONARY, nullptr, nullptr)) {
        dictionary = rw::TexDictionary::streamRead(&memory);
    }
    memory.close();
    if (dictionary == nullptr) {
        return fail(ErrorCode::Invalid, "librw could not read the texture dictionary");
    }
    return TextureDictionary(dictionary, std::move(*info), skipped);
}

TextureDictionary::TextureDictionary(TextureDictionary&& other) noexcept
    : m_dictionary(std::exchange(other.m_dictionary, nullptr)), m_info(std::move(other.m_info)),
      m_skipped(other.m_skipped) {}

TextureDictionary& TextureDictionary::operator=(TextureDictionary&& other) noexcept {
    if (this != &other) {
        destroy();
        m_dictionary = std::exchange(other.m_dictionary, nullptr);
        m_info = std::move(other.m_info);
        m_skipped = other.m_skipped;
    }
    return *this;
}

TextureDictionary::~TextureDictionary() { destroy(); }

void TextureDictionary::destroy() noexcept {
    if (m_dictionary != nullptr) {
        m_dictionary->destroy(); // destroys the textures, and with them their rasters
        m_dictionary = nullptr;
    }
}

std::vector<rw::Texture*> TextureDictionary::textures() const {
    std::vector<rw::Texture*> list;
    if (m_dictionary == nullptr) {
        return list;
    }
    // librw appends each texture it reads to the dictionary's list, so the list is in the stream's order.
    FORLIST(link, m_dictionary->textures) { list.push_back(rw::Texture::fromDict(link)); }
    return list;
}

std::expected<std::vector<RgbaImage>, Error> TextureDictionary::toImages() const {
    std::vector<RgbaImage> images;
    for (rw::Texture* texture : textures()) {
        if (texture->raster == nullptr) {
            return conversionFailure(texture, "(it has no raster)");
        }
        // Level 0 only: through librw's Image, expanded to 32 bits whatever the raster's format.
        rw::Image* image = texture->raster->toImage();
        if (image == nullptr) {
            return conversionFailure(texture, "to an image");
        }
        image->convertTo32();
        RgbaImage out{texture->name, image->width, image->height, {}};
        const auto rowBytes = static_cast<std::size_t>(image->width) * 4;
        out.pixels.resize(rowBytes * static_cast<std::size_t>(image->height));
        for (int y = 0; y < image->height; ++y) {
            std::memcpy(&out.pixels[static_cast<std::size_t>(y) * rowBytes],
                        image->pixels + static_cast<std::ptrdiff_t>(y) * image->stride, rowBytes);
        }
        image->destroy();
        images.push_back(std::move(out));
    }
    return images;
}

std::expected<void, Error> TextureDictionary::convertForDrawing() {
    CONEY_ASSERT(rw::engine != nullptr && rw::engine->device.system != rw::null::renderdevice.system);
    for (rw::Texture* texture : textures()) {
        if (texture->raster == nullptr) {
            return conversionFailure(texture, "(it has no raster)");
        }
        // convertTexToCurrentPlatform destroys the old raster when it makes a new one.
        const rw::int32 levels = texture->raster->getNumLevels();
        rw::Raster* converted = rw::Raster::convertTexToCurrentPlatform(texture->raster);
        if (converted == nullptr) {
            return conversionFailure(texture, "for drawing");
        }
        texture->raster = converted;
        keepLevels(converted, levels);
    }
    return {};
}

std::expected<void, Error> readTextureDictionaryChunk(io::Stream& chunk, const chunk::ChunkHeader& header,
                                                      chunk::ChunkStacks& stacks) {
    return readDictionaryIntoStacks(chunk, header, stacks);
}

std::expected<void, Error> readRwTextureDictionaryChunk(io::Stream& chunk, const chunk::ChunkHeader& header,
                                                        chunk::ChunkStacks& stacks) {
    return readDictionaryIntoStacks(chunk, header, stacks);
}

void addTextureDictionaryHandlers(chunk::ChunkHandlerTable& table) {
    table.setHandlers(chunk::kTextureDictionaryTid, chunk::ChunkHandlers{{}, readTextureDictionaryChunk});
    table.setHandlers(chunk::kRenderwareTextureDic, chunk::ChunkHandlers{{}, readRwTextureDictionaryChunk});
}

std::expected<std::vector<TextureDictionary>, Error>
loadTextureDictionaries(const io::Wad& wad, const io::WadEntry& entry, const chunk::ChunkHandlerTable& table) {
    std::vector<TextureDictionary> dictionaries;

    // The two RenderWare stream entries with a dictionary at a fixed place, read directly: a sector atomics file (the
    // 16-byte header, then the dictionary) and a world stream (a u32, then the dictionary).
    auto stream = wad.openEntry(entry);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    auto bytes = readAll(*stream);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    std::optional<std::size_t> dictionaryOffset;
    if (auto headered = graphics::detectHeaderedRwStream(*bytes);
        headered && headered->firstSection.id == graphics::kRwTexDictionary) {
        dictionaryOffset = graphics::kHeaderedRwStreamOffset;
    } else if (graphics::isWorldStream(*bytes)) {
        dictionaryOffset = graphics::kWorldStreamDictionaryOffset;
    }
    if (dictionaryOffset) {
        auto dictionary = TextureDictionary::read(std::span<const std::byte>(*bytes).subspan(*dictionaryOffset));
        if (!dictionary) {
            return std::unexpected(std::move(dictionary.error()));
        }
        dictionaries.push_back(std::move(*dictionary));
        return dictionaries;
    }

    // Otherwise a chunk container: load it and take every dictionary the handlers pushed.
    auto load = loadWadEntry(wad, entry, table);
    if (!load) {
        return std::unexpected(std::move(load.error()));
    }
    for (chunk::ChunkData& data : load->stacks.takeChunks(chunk::kTextureDictionaryTid)) {
        if (auto* object = dynamic_cast<TextureDictionaryObject*>(data.object.get()); object != nullptr) {
            dictionaries.push_back(std::move(object->dictionary()));
        }
    }
    if (dictionaries.empty()) {
        return fail(ErrorCode::NotFound, "the entry holds no texture dictionary");
    }
    return dictionaries;
}

} // namespace coney::platform
