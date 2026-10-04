// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_set.h"

#include <format>
#include <utility>

#include <rw.h>

#include "core/assert.h"
#include "world/world_manifest.h"
#include "world/world_streams.h"

namespace coney::platform {

namespace {

// Reads a whole WAD entry by name.
std::expected<std::vector<std::byte>, Error> readEntry(const io::Wad& wad, const std::string& name) {
    auto entry = wad.lookup(name);
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    auto stream = wad.openEntry(**entry);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream->size()));
    if (auto read = stream->read(bytes); !read) {
        return std::unexpected(std::move(read.error()));
    }
    return bytes;
}

// Whether the WAD has an entry of this name.
bool hasEntry(const io::Wad& wad, const std::string& name) { return wad.lookup(name).has_value(); }

// Prefixes an error's message with the file it came from.
std::unexpected<Error> inFile(const std::string& file, const Error& error) {
    return std::unexpected(Error{error.code, file + ": " + error.message});
}

// Reads the texture dictionary at `offset` of `file` and, for the OpenGL renderer, converts it for drawing.
std::expected<TextureDictionary, Error> readDictionary(std::span<const std::byte> file, std::size_t offset,
                                                       std::size_t bytes, bool forDrawing) {
    auto dictionary = TextureDictionary::read(file.subspan(offset, bytes));
    if (!dictionary) {
        return dictionary;
    }
    if (forDrawing) {
        if (auto converted = dictionary->convertForDrawing(); !converted) {
            return std::unexpected(std::move(converted.error()));
        }
    }
    return dictionary;
}

} // namespace

std::expected<std::vector<std::string>, Error> worldNamesFor(const io::Wad& wad, std::string_view name) {
    const std::string base(name);
    if (hasEntry(wad, base + "s_sec.wld")) {
        return std::vector<std::string>{base + "s", base + "d"};
    }
    if (hasEntry(wad, base + "_sec.wld")) {
        return std::vector<std::string>{base};
    }
    return fail(ErrorCode::NotFound, std::format("no streamed world is called {0}s, {0}d or {0}", base));
}

std::expected<std::unique_ptr<WorldSet>, Error> WorldSet::load(const io::Wad& wad, std::span<const std::string> names,
                                                               world::SectorBudget& budget, bool forDrawing) {
    installGlobalTextureLookup();
    std::unique_ptr<WorldSet> set(new WorldSet(wad, budget));
    set->m_forDrawing = forDrawing;
    for (const std::string& name : names) {
        if (auto loaded = set->loadWorld(name, forDrawing); !loaded) {
            return std::unexpected(std::move(loaded.error()));
        }
    }
    return set;
}

std::expected<void, Error> WorldSet::loadWorld(const std::string& name, bool forDrawing) {
    // The manifest first: it sizes the world's heap and every part's.
    const std::string manifestName = name + "_sec.mem";
    auto manifestBytes = readEntry(m_wad, manifestName);
    if (!manifestBytes) {
        return inFile(manifestName, manifestBytes.error());
    }
    auto manifest = world::readWorldManifest(*manifestBytes);
    if (!manifest) {
        return inFile(manifestName, manifest.error());
    }

    // Room for the world's heap, else the world is not loaded.
    if (!m_budget.reserve(manifest->worldHeapSize)) {
        return fail(ErrorCode::Invalid,
                    std::format("{}: no room for its {} bytes in the {}-byte sector budget ({} free)", name,
                                manifest->worldHeapSize, m_budget.capacity(), m_budget.freeBytes()));
    }
    const std::uint64_t heap = manifest->worldHeapSize;
    // Gives the heap back on every failure below.
    const auto failWith = [this, heap](const std::string& file, const Error& error) {
        m_budget.release(heap);
        return inFile(file, error);
    };

    // The world stream: part count, texture dictionary, then the BSP whose sectors name the parts.
    const std::string streamName = name + "_sec.wld";
    auto stream = readEntry(m_wad, streamName);
    if (!stream) {
        return failWith(streamName, stream.error());
    }
    auto layout = world::inspectWorldStream(*stream);
    if (!layout) {
        return failWith(streamName, layout.error());
    }
    auto dictionary = readDictionary(*stream, layout->dictionaryOffset, layout->dictionaryBytes, forDrawing);
    if (!dictionary) {
        return failWith(streamName, dictionary.error());
    }
    auto state = world::StreamedWorld::create(name, *layout, *manifest);
    if (!state) {
        return failWith(streamName, state.error());
    }

    rw::TexDictionary* rwDictionary = dictionary->rwDictionary();
    auto loaded = std::unique_ptr<Loaded>(new Loaded{.state = std::move(*state),
                                                     .dictionary = std::move(*dictionary),
                                                     .lookup = TextureLookupEntry(rwDictionary),
                                                     .parts = {},
                                                     .bySector = {},
                                                     .heap = heap});
    loaded->parts.resize(loaded->state.partCount());
    loaded->bySector.assign(loaded->state.sectors().size(), nullptr);
    m_states.push_back(&loaded->state);
    m_worlds.push_back(std::move(loaded));
    return {};
}

WorldSet::~WorldSet() {
    // Parts first, as World_Unload does, then the worlds (newest first, so the lookup empties in order).
    for (std::size_t w = m_worlds.size(); w-- > 0;) {
        Loaded& loaded = *m_worlds[w];
        for (std::uint32_t part = 1; part <= loaded.state.partCount(); ++part) {
            if (loaded.state.part(part).state == world::PartState::Loaded) {
                unloadPart(w, part);
                loaded.state.markPartUnloaded(part);
                m_budget.release(loaded.state.part(part).sizes.heapSize);
            }
        }
        m_budget.release(loaded.heap);
        m_worlds[w].reset();
    }
}

rw::Atomic* WorldSet::atomic(std::size_t world, std::uint32_t sector) const {
    CONEY_ASSERT(world < m_worlds.size() && sector < m_worlds[world]->bySector.size());
    return m_worlds[world]->bySector[sector];
}

std::size_t WorldSet::residentAtomics() const {
    std::size_t count = 0;
    for (const auto& loaded : m_worlds) {
        for (const Part& part : loaded->parts) {
            count += part.atomics.size();
        }
    }
    return count;
}

std::expected<void, Error> WorldSet::loadPart(std::size_t world, std::uint32_t part) {
    CONEY_ASSERT(world < m_worlds.size());
    Loaded& loaded = *m_worlds[world];
    const std::string fileName = std::format("{}_ms{}.sec", loaded.state.name(), part);
    auto file = readEntry(m_wad, fileName);
    if (!file) {
        return inFile(fileName, file.error());
    }
    auto layout = world::inspectPartFile(*file);
    if (!layout) {
        return inFile(fileName, layout.error());
    }

    // Its dictionary first, into the lookup, so the atomics' materials find their textures in it (or in any other
    // loaded dictionary).
    Part record;
    if (layout->dictionaryBytes > 0) {
        auto dictionary = readDictionary(*file, layout->dictionaryOffset, layout->dictionaryBytes, m_forDrawing);
        if (!dictionary) {
            return inFile(fileName, dictionary.error());
        }
        record.lookup.emplace(dictionary->rwDictionary());
        record.dictionary.emplace(std::move(*dictionary));
    }

    // Then each atomic, placed in its sector. A record naming a sector of another part is refused: the disc has none.
    const std::vector<world::StreamedSector>& sectors = loaded.state.sectors();
    std::vector<std::uint32_t> placed;
    for (const world::PartAtomic& entry : layout->atomics) {
        if (entry.streamedIndex >= sectors.size() || sectors[entry.streamedIndex].part != part) {
            return fail(ErrorCode::Invalid,
                        std::format("{}: an atomic names sector {}, which is not one of this part's", fileName,
                                    entry.streamedIndex));
        }
        auto atomic = WorldAtomic::read(std::span<const std::byte>(*file).subspan(entry.offset, entry.bytes),
                                        sectors[entry.streamedIndex].origin);
        if (!atomic) {
            return inFile(fileName, atomic.error());
        }
        atomic->unpack();
        // Modulate by the material colour, whose alpha fades the atomic in (World_ReadPart sets flag 0x40).
        atomic->atomic()->geometry->flags |= rw::Geometry::MODULATE;
        record.atomics.push_back(std::move(*atomic));
        placed.push_back(entry.streamedIndex);
    }
    for (std::size_t i = 0; i < placed.size(); ++i) {
        loaded.bySector[placed[i]] = record.atomics[i].atomic();
    }
    loaded.parts[part - 1] = std::move(record);
    return {};
}

void WorldSet::unloadPart(std::size_t world, std::uint32_t part) {
    CONEY_ASSERT(world < m_worlds.size());
    Loaded& loaded = *m_worlds[world];
    for (const std::uint32_t sector : loaded.state.part(part).sectors) {
        loaded.bySector[sector] = nullptr;
    }
    // Atomics, then the lookup entry, then the dictionary their materials took textures from.
    Part& record = loaded.parts[part - 1];
    record.atomics.clear();
    record.lookup.reset();
    record.dictionary.reset();
}

} // namespace coney::platform
