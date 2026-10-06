// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/level_file.h"

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <rw.h>

#include "platform/texture_dictionary.h"
#include "platform/texture_lookup.h"
#include "world/world_streams.h"

namespace coney::platform {

namespace {

// Reads the whole chunk window into memory.
std::expected<std::vector<std::byte>, Error> readChunk(io::Stream& chunk) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(chunk.size()));
    if (auto read = chunk.read(bytes); !read) {
        return std::unexpected(std::move(read.error()));
    }
    return bytes;
}

// The dictionary object of `object`, if it is one.
TextureDictionaryObject* asDictionary(chunk::LoadedObject* object) {
    return dynamic_cast<TextureDictionaryObject*>(object);
}

} // namespace

rw::Atomic* levelAtomic(const chunk::LoadedObject* object) {
    const auto* level = dynamic_cast<const LevelAtomicObject*>(object);
    return level == nullptr ? nullptr : level->atomic();
}

std::expected<void, Error> readPreinstanceObjectChunk(io::Stream& chunk, const chunk::ChunkHeader& header,
                                                      chunk::ChunkStacks& stacks) {
    auto bytes = readChunk(chunk);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    auto models = world::extractClumpModels(*bytes);
    if (!models) {
        return std::unexpected(std::move(models.error()));
    }
    std::vector<LevelClumpObject::Part> parts;
    for (const world::ClumpModel& model : *models) {
        auto atomic = WorldAtomic::read(model.atomicSection, world::Vec3{});
        if (!atomic) {
            return std::unexpected(std::move(atomic.error()));
        }
        atomic->setTransform(model.frame);
        atomic->unpack();
        parts.push_back(LevelClumpObject::Part{std::move(*atomic), model.frame});
    }
    std::unique_ptr<chunk::LoadedObject> object;
    if (parts.size() == 1) {
        object =
            std::make_unique<LevelAtomicObject>(std::move(parts.front().atomic), parts.front().frame, "level model");
    } else {
        object = std::make_unique<LevelClumpObject>(std::move(parts));
    }
    stacks.pushChunk(
        chunk::ChunkData{.type = world::kLevelModelResult, .id = header.id, .bytes = {}, .object = std::move(object)});
    return {};
}

std::expected<void, Error> readSectorBspChunk(io::Stream& chunk, const chunk::ChunkHeader& header,
                                              chunk::ChunkStacks& stacks) {
    auto bytes = readChunk(chunk);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    auto world = world::extractLevelWorld(*bytes);
    if (!world) {
        return std::unexpected(std::move(world.error()));
    }
    // The glow sprites' dictionary is the newest one on the stack: register it while librw binds the textures.
    std::optional<TextureLookupEntry> lookup;
    const auto chunks = stacks.chunks();
    for (auto it = chunks.rbegin(); it != chunks.rend(); ++it) {
        if (it->type == chunk::kTextureDictionaryTid) {
            if (TextureDictionaryObject* dictionary = asDictionary(it->object.get()); dictionary != nullptr) {
                lookup.emplace(dictionary->dictionary().rwDictionary());
            }
            break;
        }
    }
    auto atomic = WorldAtomic::read(world->atomicSection, world::Vec3{}, VertexLayout::RenderWareDefault);
    lookup.reset();
    if (!atomic) {
        return std::unexpected(std::move(atomic.error()));
    }
    atomic->unpack();
    stacks.pushChunk(chunk::ChunkData{
        .type = world::kLevelWorldResult,
        .id = header.id,
        .bytes = {},
        .object = std::make_unique<LevelAtomicObject>(std::move(*atomic), world::FrameMatrix{}, "level world")});
    return {};
}

std::expected<void, Error> linkLevelModel(chunk::LoadedObject& model, chunk::LoadedObject& dictionary) {
    rw::Atomic* atomic = levelAtomic(&model);
    TextureDictionaryObject* textures = asDictionary(&dictionary);
    if (atomic == nullptr || textures == nullptr) {
        return fail(ErrorCode::Invalid,
                    std::format("a level model links a {} to a {}", model.describe(), dictionary.describe()));
    }
    const std::vector<rw::Texture*> list = textures->dictionary().textures();
    rw::Geometry* geometry = atomic->geometry;
    if (list.empty() || geometry->matList.numMaterials < 1) {
        return fail(ErrorCode::Invalid, "a level model or its dictionary is empty");
    }
    geometry->matList.materials[0]->setTexture(list.front());
    return {};
}

void addLevelFileReaders(chunk::ChunkHandlerTable& table) {
    addTextureDictionaryHandlers(table);
    table.setHandlers(world::kPreinstanceObjectChunk, chunk::ChunkHandlers{{}, readPreinstanceObjectChunk});
    table.setHandlers(world::kSectorBspDataChunk, chunk::ChunkHandlers{{}, readSectorBspChunk});
    world::addLevelFileHandlers(table, linkLevelModel);
}

std::expected<std::unique_ptr<world::LevelObject>, Error> loadLevel(const io::Wad& wad, std::string_view level,
                                                                    bool forDrawing) {
    installGlobalTextureLookup();
    const std::string file = std::string(level) + ".lev";
    auto entry = wad.lookup(file);
    if (!entry) {
        return fail(ErrorCode::NotFound, std::format("{}: not in the WAD", file));
    }
    auto stream = wad.openEntry(**entry);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    addLevelFileReaders(table);
    auto loaded = world::loadLevelFile(*stream, table);
    if (!loaded) {
        return std::unexpected(Error{loaded.error().code, file + ": " + loaded.error().message});
    }
    if (forDrawing) {
        world::LevelObject& object = **loaded;
        for (chunk::LoadedObject* dictionary : {object.glowDictionary.get(), object.skyline.dictionary.get(),
                                                object.skyBox.dictionary.get(), object.cloudBox.dictionary.get()}) {
            if (TextureDictionaryObject* textures = asDictionary(dictionary); textures != nullptr) {
                if (auto converted = textures->dictionary().convertForDrawing(); !converted) {
                    return std::unexpected(Error{converted.error().code, file + ": " + converted.error().message});
                }
            }
        }
    }
    return loaded;
}

} // namespace coney::platform
