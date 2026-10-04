// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/particle_page.h"

#include <format>
#include <utility>

#include "core/assert.h"
#include "fileio/reader.h"

namespace coney::graphics {

const UvRect& ParticlePage::rect(std::size_t index) const {
    CONEY_ASSERT(index < rects.size());
    return rects[index];
}

std::expected<ParticlePage, Error> parseParticlePage(std::span<const std::byte> data) {
    if (data.size() < kParticlePageHeaderSize) {
        return fail(ErrorCode::Truncated, std::format("a particle page needs a {}-byte header, the chunk has {} bytes",
                                                      kParticlePageHeaderSize, data.size()));
    }
    // The header: +0x00 a placeholder from the build, +0x04 the count, +0x08 firstGlyph; +0x0c and +0x10 are zero
    // in the file and set by the original's loader to its own pointers.
    io::Reader reader(data);
    const std::uint32_t count = io::loadU32Le(data.subspan(4, 4));
    const auto firstGlyph = static_cast<std::int32_t>(io::loadU32Le(data.subspan(8, 4)));
    // Check the size before allocating, so a damaged count cannot reserve gigabytes.
    if ((data.size() - kParticlePageHeaderSize) / kParticlePageRectSize < count) {
        return fail(ErrorCode::Truncated,
                    std::format("a particle page of {} bytes cannot hold its {} rectangles", data.size(), count));
    }
    if (auto moved = reader.seek(kParticlePageHeaderSize); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    ParticlePage page;
    page.firstGlyph = firstGlyph;
    page.rects.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        // The size check above covers every read in this loop, so these cannot fail.
        UvRect rect;
        rect.u0 = reader.readF32Le().value();
        rect.v0 = reader.readF32Le().value();
        rect.u1 = reader.readF32Le().value();
        rect.v1 = reader.readF32Le().value();
        page.rects.push_back(rect);
    }
    return page;
}

const SheetTableRecord& SpriteSheetTable::record(std::size_t index) const {
    CONEY_ASSERT(index < records.size());
    return records[index];
}

std::optional<std::uint32_t> SpriteSheetTable::sizeOf(std::uint32_t nameHash) const {
    for (const SheetTableRecord& entry : records) {
        if (entry.nameHash == nameHash) {
            return entry.size;
        }
    }
    return std::nullopt;
}

std::expected<SpriteSheetTable, Error> parseSpriteSheetTable(std::span<const std::byte> data) {
    constexpr std::size_t kRecordSize = 8;
    if (data.size() < 4) {
        return fail(ErrorCode::Truncated, "a sprite sheet table needs its 4-byte count");
    }
    const std::uint32_t count = io::loadU32Le(data.first(4));
    // Check the size before allocating, so a damaged count cannot reserve gigabytes.
    if ((data.size() - 4) / kRecordSize < count) {
        return fail(ErrorCode::Truncated,
                    std::format("a sprite sheet table of {} bytes cannot hold its {} records", data.size(), count));
    }
    SpriteSheetTable table;
    table.records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::span<const std::byte> bytes = data.subspan(4 + i * kRecordSize, kRecordSize);
        table.records.push_back(SheetTableRecord{io::loadU32Le(bytes.first(4)), io::loadU32Le(bytes.subspan(4, 4))});
    }
    return table;
}

std::expected<void, Error> onParticlePageHeaderLoaded(chunk::ChunkStacks& stacks, std::uint32_t type) {
    auto chunk = stacks.popChunk(type);
    if (!chunk) {
        return std::unexpected(std::move(chunk.error()));
    }
    auto table = parseSpriteSheetTable(chunk->bytes);
    if (!table) {
        stacks.pushChunk(std::move(*chunk)); // leave the stack as it was
        return std::unexpected(std::move(table.error()));
    }
    chunk::ChunkData result;
    result.type = kParticlePageHeader;
    result.id = chunk->id;
    result.object = std::make_unique<SpriteSheetTableObject>(std::move(*table));
    stacks.pushChunk(std::move(result));
    return {};
}

} // namespace coney::graphics
