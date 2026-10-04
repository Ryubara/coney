// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/rw_stream.h"

#include <format>
#include <string_view>
#include <utility>

namespace coney::graphics {

namespace {

// RenderWare raster format bits, as librw names them (Raster::C1555 and so on in its rwobjects.h).
constexpr std::uint32_t kRasterTypeMask = 0x0007;
constexpr std::uint32_t kRasterTypeTexture = 0x0004;
constexpr std::uint32_t kPixelFormatMask = 0x0F00;
constexpr std::uint32_t kFormatC1555 = 0x0100;
constexpr std::uint32_t kFormatC8888 = 0x0500;
constexpr std::uint32_t kFormatC888 = 0x0600;
constexpr std::uint32_t kFormatC555 = 0x0A00;
constexpr std::uint32_t kPaletteMask = 0x6000;
constexpr std::uint32_t kPalette8 = 0x2000;
constexpr std::uint32_t kPalette4 = 0x4000;

// The longest name librw keeps: its name and mask fields are 32 bytes, and it copies a string section into them
// whole, so a longer section would overrun the field.
constexpr std::uint32_t kMaxNameBytes = 32;

// Bytes of the raster header inside a PS2 native texture (librw's StreamRasterExt).
constexpr std::uint32_t kPs2RasterHeaderBytes = 64;

// Largest texture side accepted. The PS2's GS addresses at most 2048 texels a side; anything bigger is damage.
constexpr std::uint32_t kMaxTextureSide = 2048;

// One section inside a parent: its header and a view of exactly its data.
struct Section {
    RwSectionHeader header;
    std::span<const std::byte> data;
};

// Reads the next section from `reader`, which views the parent's data, and checks that it fits inside the parent.
// `what` names the expected section in error messages.
std::expected<Section, Error> readSection(io::Reader& reader, std::uint32_t expectedId, std::string_view what) {
    const std::size_t offset = reader.position();
    auto header = readRwSectionHeader(reader);
    if (!header) {
        return fail(ErrorCode::Truncated,
                    std::format("{} at offset {:#x}: the section header is cut off", what, offset));
    }
    if (header->id != expectedId) {
        return fail(ErrorCode::Invalid, std::format("{} at offset {:#x}: expected section {:#x} but found {:#x}", what,
                                                    offset, expectedId, header->id));
    }
    auto data = reader.readBytes(header->size);
    if (!data) {
        return fail(ErrorCode::Truncated,
                    std::format("{} at offset {:#x}: {} bytes of data but only {} remain in its parent", what, offset,
                                header->size, reader.remaining()));
    }
    return Section{*header, *data};
}

// Reads a string section (a texture's name or mask): null-terminated and padded, at most kMaxNameBytes.
std::expected<std::string, Error> readName(io::Reader& reader, std::string_view what) {
    auto section = readSection(reader, kRwString, what);
    if (!section) {
        return std::unexpected(std::move(section.error()));
    }
    if (section->header.size > kMaxNameBytes) {
        return fail(ErrorCode::Invalid, std::format("{} is {} bytes, more than the {} librw can hold", what,
                                                    section->header.size, kMaxNameBytes));
    }
    std::string name;
    for (const std::byte b : section->data) {
        if (b == std::byte{0}) {
            break;
        }
        name.push_back(static_cast<char>(b));
    }
    return name;
}

// Checks the raster format bits against what librw's PS2 raster code can create and convert, so that it never reaches
// one of librw's asserts. The depth must agree with the format: palettes are 4 or 8 bits, true colour 16 or 32.
std::expected<void, Error> checkRasterFormat(const Ps2TextureInfo& info, std::string_view where) {
    const std::uint32_t type = info.rasterFormat & kRasterTypeMask;
    const std::uint32_t pixel = info.rasterFormat & kPixelFormatMask;
    const std::uint32_t palette = info.rasterFormat & kPaletteMask;
    const auto bad = [&](std::string_view why) {
        return fail(ErrorCode::Invalid, std::format("{} (\"{}\"): raster format {:#06x} with depth {}: {}", where,
                                                    info.name, info.rasterFormat, info.depth, why));
    };
    if (type != kRasterTypeTexture) {
        return bad("not a texture raster");
    }
    if (info.width == 0 || info.height == 0 || info.width > kMaxTextureSide || info.height > kMaxTextureSide) {
        return bad(std::format("size {}x{} is outside 1 to {}", info.width, info.height, kMaxTextureSide));
    }
    if (palette == kPalette8 || palette == kPalette4) {
        if (pixel != kFormatC8888 && pixel != kFormatC1555) {
            return bad("a palette must hold 32-bit or 16-bit colours");
        }
        if (info.depth != (palette == kPalette8 ? 8U : 4U)) {
            return bad("the depth does not match the palette size");
        }
        return {};
    }
    if (palette != 0) {
        return bad("both palette bits are set");
    }
    if (pixel == kFormatC8888 || pixel == kFormatC888) {
        // librw stores 24-bit colour in 32-bit texels on the PS2 and asserts on depth 24.
        return info.depth == 32 ? std::expected<void, Error>{} : bad("32-bit colour needs depth 32");
    }
    if (pixel == kFormatC1555 || pixel == kFormatC555) {
        return info.depth == 16 ? std::expected<void, Error>{} : bad("16-bit colour needs depth 16");
    }
    return bad("librw cannot convert this pixel format from the PS2");
}

// Reads one texture native section's data: platform header, name, mask, raster header and pixels, then the
// texture's extension. `index` numbers the texture in error messages.
std::expected<Ps2TextureInfo, Error> readPs2Texture(std::span<const std::byte> data, std::size_t index) {
    const std::string where = std::format("texture {}", index);
    io::Reader reader(data);
    Ps2TextureInfo info;

    // The platform header: "PS2\0" and the filter and addressing word.
    auto platform = readSection(reader, kRwStruct, where + " platform header");
    if (!platform) {
        return std::unexpected(std::move(platform.error()));
    }
    io::Reader platformReader(platform->data);
    auto fourcc = platformReader.readU32Le();
    auto filter = platformReader.readU32Le();
    if (!fourcc || !filter) {
        return fail(ErrorCode::Truncated, where + ": the platform header is shorter than 8 bytes");
    }
    if (*fourcc != kPs2NativeTexture) {
        return fail(ErrorCode::Invalid,
                    std::format("{}: platform {:#010x} is not a PS2 native texture", where, *fourcc));
    }
    info.filterAddressing = *filter;

    // Name and mask.
    auto name = readName(reader, where + " name");
    if (!name) {
        return std::unexpected(std::move(name.error()));
    }
    info.name = std::move(*name);
    auto mask = readName(reader, where + " mask");
    if (!mask) {
        return std::unexpected(std::move(mask.error()));
    }
    info.mask = std::move(*mask);

    // The raster: one struct section holding the 64-byte raster header and the pixel data, each a struct of its own.
    auto raster = readSection(reader, kRwStruct, where + " raster");
    if (!raster) {
        return std::unexpected(std::move(raster.error()));
    }
    io::Reader rasterReader(raster->data);
    auto header = readSection(rasterReader, kRwStruct, where + " raster header");
    if (!header) {
        return std::unexpected(std::move(header.error()));
    }
    if (header->header.size != kPs2RasterHeaderBytes) {
        return fail(ErrorCode::Invalid, std::format("{}: the raster header is {} bytes, not {}", where,
                                                    header->header.size, kPs2RasterHeaderBytes));
    }
    // The size check above covers every read of the header.
    info.rasterLibraryStamp = header->header.libraryStamp;
    io::Reader fields(header->data);
    info.width = fields.readU32Le().value();
    info.height = fields.readU32Le().value();
    info.depth = fields.readU32Le().value();
    info.rasterFormat = fields.readU16Le().value();
    info.version = static_cast<std::int16_t>(fields.readU16Le().value());
    // Skip TEX0 (8 bytes), the palette offset, TEX1's low word and the two MIPTBP registers (8 each): 32 bytes.
    (void)fields.readBytes(32).value();
    info.pixelBytes = fields.readU32Le().value();
    info.paletteBytes = fields.readU32Le().value();
    if (auto checked = checkRasterFormat(info, where); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    auto pixels = readSection(rasterReader, kRwStruct, where + " pixels");
    if (!pixels) {
        return std::unexpected(std::move(pixels.error()));
    }
    // librw copies the pixels and the palette out of this section by the sizes in the raster header (or, for the older
    // layout, the whole section into a buffer of those sizes), so the three must agree.
    if (std::uint64_t{info.pixelBytes} + info.paletteBytes != pixels->header.size) {
        return fail(ErrorCode::Invalid,
                    std::format("{}: the raster header gives {} + {} bytes of pixels and palette but the section "
                                "holds {}",
                                where, info.pixelBytes, info.paletteBytes, pixels->header.size));
    }

    // The texture's extension (plugin data), which librw reads next.
    if (auto extension = readSection(reader, kRwExtension, where + " extension"); !extension) {
        return std::unexpected(std::move(extension.error()));
    }
    return info;
}

} // namespace

std::expected<RwSectionHeader, Error> readRwSectionHeader(io::Reader& reader) {
    if (reader.remaining() < kRwHeaderSize) {
        return fail(ErrorCode::Truncated, std::format("a RenderWare section header needs {} bytes but only {} remain",
                                                      kRwHeaderSize, reader.remaining()));
    }
    // The size check covers all three reads.
    RwSectionHeader header;
    header.id = reader.readU32Le().value();
    header.size = reader.readU32Le().value();
    header.libraryStamp = reader.readU32Le().value();
    return header;
}

std::optional<HeaderedRwStream> detectHeaderedRwStream(std::span<const std::byte> entryStart) {
    if (entryStart.size() < kHeaderedRwStreamOffset + kRwHeaderSize) {
        return std::nullopt;
    }
    io::Reader reader(entryStart);
    // The size check covers the four header reads.
    const std::uint32_t count = reader.readU32Le().value();
    const std::uint32_t second = reader.readU32Le().value();
    const std::uint32_t third = reader.readU32Le().value();
    const std::uint32_t id = reader.readU32Le().value();
    if (count != 1 || second != 0 || third != 0) {
        return std::nullopt;
    }
    auto section = readRwSectionHeader(reader);
    if (!section || section->libraryStamp != kRwLibraryStamp) {
        return std::nullopt;
    }
    return HeaderedRwStream{id, *section};
}

bool isWorldStream(std::span<const std::byte> entryStart) {
    if (entryStart.size() < kWorldStreamDictionaryOffset + kRwHeaderSize) {
        return false;
    }
    io::Reader reader(entryStart.subspan(kWorldStreamDictionaryOffset));
    auto section = readRwSectionHeader(reader);
    return section && section->id == kRwTexDictionary && section->libraryStamp == kRwLibraryStamp;
}

std::expected<std::size_t, Error> findRwSection(std::span<const std::byte> stream, std::uint32_t id) {
    io::Reader reader(stream);
    while (reader.remaining() > 0) {
        const std::size_t offset = reader.position();
        auto header = readRwSectionHeader(reader);
        if (!header) {
            return std::unexpected(std::move(header.error()));
        }
        if (header->id == id) {
            return offset;
        }
        if (auto skipped = reader.readBytes(header->size); !skipped) {
            return fail(ErrorCode::Truncated,
                        std::format("RenderWare section {:#x} at offset {:#x} runs past the end", header->id, offset));
        }
    }
    return fail(ErrorCode::NotFound, std::format("no RenderWare section {:#x} in the stream", id));
}

std::expected<TexDictionaryInfo, Error> inspectTexDictionary(std::span<const std::byte> stream) {
    io::Reader reader(stream);
    auto dictionary = readSection(reader, kRwTexDictionary, "texture dictionary");
    if (!dictionary) {
        return std::unexpected(std::move(dictionary.error()));
    }
    TexDictionaryInfo info;
    info.libraryStamp = dictionary->header.libraryStamp;
    info.streamBytes = kRwHeaderSize + dictionary->header.size;

    // The dictionary's own struct: the texture count and the device id.
    io::Reader body(dictionary->data);
    auto header = readSection(body, kRwStruct, "texture dictionary header");
    if (!header) {
        return std::unexpected(std::move(header.error()));
    }
    io::Reader headerReader(header->data);
    auto count = headerReader.readU16Le();
    auto device = headerReader.readU16Le();
    if (!count || !device) {
        return fail(ErrorCode::Truncated, "the texture dictionary header is shorter than 4 bytes");
    }
    info.deviceId = *device;

    // Each texture, checked in turn; a section's size is checked against what is left before it is read.
    info.textures.reserve(*count);
    for (std::size_t i = 0; i < *count; ++i) {
        // The section's offset in the whole stream: the dictionary's own header comes before its body.
        const std::size_t sectionOffset = kRwHeaderSize + body.position();
        auto native = readSection(body, kRwTextureNative, std::format("texture {}", i));
        if (!native) {
            return std::unexpected(std::move(native.error()));
        }
        auto texture = readPs2Texture(native->data, i);
        if (!texture) {
            return std::unexpected(std::move(texture.error()));
        }
        texture->sectionOffset = sectionOffset;
        texture->sectionBytes = kRwHeaderSize + native->header.size;
        info.textures.push_back(std::move(*texture));
    }

    // librw fails the whole dictionary unless the dictionary's own extension follows the textures.
    if (auto extension = readSection(body, kRwExtension, "texture dictionary extension"); !extension) {
        return std::unexpected(std::move(extension.error()));
    }
    return info;
}

} // namespace coney::graphics
