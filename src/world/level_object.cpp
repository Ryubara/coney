// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/level_object.h"

#include <bit>
#include <format>
#include <utility>

#include "fileio/reader.h"

namespace coney::world {

namespace {

// Occluder records: the header before them, each record's size, where its four points start.
constexpr std::size_t kOccluderHeaderBytes = 16;
constexpr std::size_t kOccluderRecordBytes = 0x70;
constexpr std::size_t kOccluderPointsOffset = 0x30;

// Path data: the header and each record kind's size.
constexpr std::size_t kPathHeaderBytes = 16;
constexpr std::size_t kPathRecordBytes = 0x50;
constexpr std::size_t kPathABytes = 16;
constexpr std::size_t kPathBBytes = 16;
constexpr std::size_t kPathCBytes = 32;
constexpr std::size_t kPathDBytes = 8;

// A float of a buffer whose size the caller has checked.
float loadF32(std::span<const std::byte> bytes, std::size_t at) {
    return std::bit_cast<float>(io::loadU32Le(bytes.subspan(at, 4)));
}

// Pops the chunk of `type` and returns its object, which a stream reader must have pushed.
std::expected<std::unique_ptr<chunk::LoadedObject>, Error> popChunkObject(chunk::ChunkStacks& stacks,
                                                                          std::uint32_t type, std::string_view what) {
    auto chunk = stacks.popChunk(type);
    if (!chunk) {
        return std::unexpected(
            Error{chunk.error().code, std::format("the level's {}: {}", what, chunk.error().message)});
    }
    if (!chunk->object) {
        return fail(ErrorCode::Invalid, std::format("the level's {} was not read by its reader", what));
    }
    return std::move(chunk->object);
}

// Pops a model (0x41) and its dictionary (0x0B), and links them.
std::expected<LevelModel, Error> popLinkedModel(chunk::ChunkStacks& stacks, LinkLevelModel link,
                                                std::string_view what) {
    LevelModel pair;
    auto model = popChunkObject(stacks, kLevelModelResult, what);
    if (!model) {
        return std::unexpected(std::move(model.error()));
    }
    auto dictionary = popChunkObject(stacks, chunk::kTextureDictionaryTid, what);
    if (!dictionary) {
        return std::unexpected(std::move(dictionary.error()));
    }
    // @orig 0x0040cdd8 LevelObject_LinkModel (unknown)
    if (auto linked = link(**model, **dictionary); !linked) {
        return std::unexpected(std::move(linked.error()));
    }
    pair.model = std::move(*model);
    pair.dictionary = std::move(*dictionary);
    return pair;
}

} // namespace

std::expected<std::vector<Occluder>, Error> readOccluders(std::span<const std::byte> chunk) {
    if (chunk.size() < kOccluderHeaderBytes) {
        return fail(ErrorCode::Truncated, "the occluders chunk is shorter than its header");
    }
    const std::uint32_t count = io::loadU32Le(chunk);
    if (std::uint64_t{count} * kOccluderRecordBytes > chunk.size() - kOccluderHeaderBytes) {
        return fail(ErrorCode::Truncated,
                    std::format("{} occluders do not fit in the chunk's {} bytes", count, chunk.size()));
    }
    std::vector<Occluder> occluders(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t record = kOccluderHeaderBytes + std::size_t{i} * kOccluderRecordBytes;
        for (std::size_t k = 0; k < 4; ++k) {
            const std::size_t at = record + kOccluderPointsOffset + k * 16;
            // Game axes (z up) to RenderWare's (y up): (x, y, z) -> (x, z, -y).
            occluders[i].corners.at(k) = Vec3{loadF32(chunk, at), loadF32(chunk, at + 8), -loadF32(chunk, at + 4)};
        }
    }
    return occluders;
}

std::expected<PathDataHeader, Error> inspectPathData(std::span<const std::byte> chunk) {
    if (chunk.size() < kPathHeaderBytes) {
        return fail(ErrorCode::Truncated, "the path data chunk is shorter than its header");
    }
    PathDataHeader header;
    header.paths = io::loadU32Le(chunk);
    header.bCount = io::loadU32Le(chunk.subspan(4));
    header.cCount = static_cast<std::uint16_t>(io::loadU32Le(chunk.subspan(8)) & 0xFFFFU);
    header.aCount = static_cast<std::uint16_t>(io::loadU32Le(chunk.subspan(8)) >> 16U);
    header.dCount = io::loadU32Le(chunk.subspan(12));
    // The C and A counts are signed in the original; negative ones would make no sense.
    if ((header.cCount & 0x8000U) != 0 || (header.aCount & 0x8000U) != 0) {
        return fail(ErrorCode::Invalid, "the path data has a negative record count");
    }
    const std::uint64_t bytes = kPathHeaderBytes + std::uint64_t{header.aCount} * kPathABytes +
                                std::uint64_t{header.bCount} * kPathBBytes +
                                std::uint64_t{header.paths} * kPathRecordBytes +
                                std::uint64_t{header.cCount} * kPathCBytes + std::uint64_t{header.dCount} * kPathDBytes;
    if (bytes > chunk.size()) {
        return fail(ErrorCode::Truncated,
                    std::format("the path data's records need {} bytes; the chunk has {}", bytes, chunk.size()));
    }
    header.recordBytes = static_cast<std::size_t>(bytes);
    return header;
}

std::expected<void, Error> onPathDataLoaded(chunk::ChunkStacks& stacks, std::uint32_t /*type*/) {
    auto chunk = stacks.popChunk(kPathDataChunk);
    if (!chunk) {
        return std::unexpected(std::move(chunk.error()));
    }
    auto header = inspectPathData(chunk->bytes);
    if (!header) {
        return std::unexpected(std::move(header.error()));
    }
    stacks.pushObject(std::make_unique<PathDataObject>(*header, std::move(chunk->bytes)));
    return {};
}

chunk::OnLoaded makeLevelHeaderHandler(LinkLevelModel link) {
    return [link](chunk::ChunkStacks& stacks, std::uint32_t /*type*/) -> std::expected<void, Error> {
        // The header itself: 48 bytes of the tool's stale pointers, which the original overwrites with the object.
        auto header = stacks.popChunk(kLevelHeaderChunk);
        if (!header) {
            return std::unexpected(std::move(header.error()));
        }
        if (header->bytes.size() != kLevelHeaderBytes) {
            return fail(ErrorCode::Invalid,
                        std::format("level header of {} bytes, expected {}", header->bytes.size(), kLevelHeaderBytes));
        }
        auto level = std::make_unique<LevelObject>();

        // The objects: the collision mesh (pushed last), then the path data.
        auto collision = stacks.popObject<raycast::CollisionMesh>();
        if (!collision) {
            return std::unexpected(std::move(collision.error()));
        }
        level->collision = std::move(*collision);
        auto paths = stacks.popObject<PathDataObject>();
        if (!paths) {
            return std::unexpected(std::move(paths.error()));
        }
        level->pathHeader = (*paths)->header();
        level->pathData = std::move((*paths)->bytes());

        // The chunks, in reverse file order: the glow world and its dictionary, then the three linked models.
        auto world = popChunkObject(stacks, kLevelWorldResult, "level world");
        if (!world) {
            return std::unexpected(std::move(world.error()));
        }
        level->levelWorld = std::move(*world);
        auto glows = popChunkObject(stacks, chunk::kTextureDictionaryTid, "glow dictionary");
        if (!glows) {
            return std::unexpected(std::move(glows.error()));
        }
        level->glowDictionary = std::move(*glows);
        for (auto [slot, what] : {std::pair{&level->cloudBox, "cloud box"}, std::pair{&level->skyBox, "sky box"},
                                  std::pair{&level->skyline, "skyline"}}) {
            auto pair = popLinkedModel(stacks, link, what);
            if (!pair) {
                return std::unexpected(std::move(pair.error()));
            }
            *slot = std::move(*pair);
        }

        // The occluders, the file's first chunk.
        auto occluders = stacks.popChunk(kOccludersChunk);
        if (!occluders) {
            return std::unexpected(std::move(occluders.error()));
        }
        auto read = readOccluders(occluders->bytes);
        if (!read) {
            return std::unexpected(std::move(read.error()));
        }
        level->occluders = std::move(*read);
        stacks.pushObject(std::move(level));
        return {};
    };
}

void addLevelFileHandlers(chunk::ChunkHandlerTable& table, LinkLevelModel link) {
    table.setHandlers(raycast::kCollisionMeshChunk, chunk::ChunkHandlers{raycast::onCollisionMeshLoaded, {}});
    table.setHandlers(kPathDataChunk, chunk::ChunkHandlers{onPathDataLoaded, {}});
    table.setHandlers(kLevelHeaderChunk, chunk::ChunkHandlers{makeLevelHeaderHandler(link), {}});
}

std::expected<std::unique_ptr<LevelObject>, Error> loadLevelFile(io::Stream& stream,
                                                                 const chunk::ChunkHandlerTable& table) {
    chunk::ChunkStacks stacks;
    if (auto loaded = chunk::loadContainer(stream, table, stacks); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    auto level = stacks.popObject<LevelObject>();
    if (!level) {
        return std::unexpected(std::move(level.error()));
    }
    // The subtitles stay raw on the chunk stack (the original's handler keeps them in a global); the level takes them.
    for (chunk::ChunkData& subtitles : stacks.takeChunks(kSubtitlesChunk)) {
        (*level)->subtitles = std::move(subtitles.bytes);
    }
    if (!stacks.chunks().empty() || !stacks.objects().empty()) {
        return fail(ErrorCode::Invalid, std::format("the level file left {} chunks and {} objects that no handler took",
                                                    stacks.chunks().size(), stacks.objects().size()));
    }
    return std::move(*level);
}

} // namespace coney::world
