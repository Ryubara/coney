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

std::vector<std::string> playableLevelNames(const io::Wad& wad) {
    // Every level is named after its number; 200 is well past the highest the disc holds.
    constexpr int kLevelNumbers = 200;
    std::vector<std::string> names;
    for (int number = 0; number < kLevelNumbers; ++number) {
        std::string name = std::format("level{}", number);
        if (worldNamesFor(wad, name)) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

std::expected<std::unique_ptr<WorldSet>, Error> WorldSet::load(const io::Wad& wad, std::span<const std::string> names,
                                                               world::SectorBudget& budget, bool forDrawing,
                                                               SceneAssets& assets) {
    installGlobalTextureLookup();
    std::unique_ptr<WorldSet> set(new WorldSet(wad, budget, assets));
    set->m_forDrawing = forDrawing;
    for (const std::string& name : names) {
        if (auto loaded = set->loadWorld(name, forDrawing); !loaded) {
            set->dropTextures(set->m_readTextures);
            return std::unexpected(std::move(loaded.error()));
        }
        // What the world holds in the scene assets: its dictionary's textures now, its parts' and sectors' as they
        // load.
        const world::StreamedWorld& state = set->m_worlds.back()->state;
        set->m_held.push_back(Held{.textures = std::exchange(set->m_readTextures, {}),
                                   .parts = std::vector<std::vector<graphics::TextureId>>(state.partCount()),
                                   .meshes = std::vector<std::optional<SectorMesh>>(state.sectors().size())});
    }
    return set;
}

std::expected<TextureDictionary, Error> WorldSet::readDictionary(std::span<const std::byte> file, std::size_t offset,
                                                                 std::size_t bytes, bool forDrawing) {
    auto dictionary = TextureDictionary::read(file.subspan(offset, bytes));
    if (!dictionary) {
        return dictionary;
    }
    // Its textures into the scene assets first, while a PS2 raster is still as loaded (docs/engine/asset-formats.md);
    // one that cannot be converted is left out, and the materials naming it get no texture id.
    rw::TexDictionary* rwDictionary = dictionary->rwDictionary();
    FORLIST(link, rwDictionary->textures) {
        if (auto id = m_assets.addTexture(*rw::Texture::fromDict(link)); id && id->valid()) {
            m_readTextures.push_back(*id);
        }
    }
    if (forDrawing) {
        if (auto converted = dictionary->convertForDrawing(); !converted) {
            return std::unexpected(std::move(converted.error()));
        }
    }
    return dictionary;
}

void WorldSet::dropTextures(std::vector<graphics::TextureId>& textures) {
    for (const graphics::TextureId id : textures) {
        m_assets.removeTexture(id);
    }
    textures.clear();
}

WorldSet::SectorMesh WorldSet::addMeshes(rw::Atomic* atomic) {
    // A mesh that cannot be converted keeps no id; the world renderer then draws its atomic as before.
    SectorMesh mesh;
    if (auto id = m_assets.addAtomic(*atomic); id) {
        mesh.mesh = *id;
    }
    if (rw::Atomic* layer = dualLayerOf(atomic); layer != nullptr) {
        if (auto id = m_assets.addAtomic(*layer); id) {
            mesh.dualLayer = *id;
        }
    }
    return mesh;
}

void WorldSet::removeMeshes(const SectorMesh& mesh) {
    m_assets.removeMesh(mesh.mesh);
    m_assets.removeMesh(mesh.dualLayer);
}

// Gives back every texture still held for a world or one of its parts.
WorldSet::HeldTextures::~HeldTextures() {
    for (Held& world : held) {
        for (const graphics::TextureId id : world.textures) {
            assets.removeTexture(id);
        }
        for (const std::vector<graphics::TextureId>& part : world.parts) {
            for (const graphics::TextureId id : part) {
                assets.removeTexture(id);
            }
        }
        for (const std::optional<SectorMesh>& mesh : world.meshes) {
            if (mesh) {
                assets.removeMesh(mesh->mesh);
                assets.removeMesh(mesh->dualLayer);
            }
        }
    }
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
    return m_worlds[world]->drawn ? m_worlds[world]->bySector[sector] : nullptr;
}

rw::Atomic* WorldSet::loadedAtomic(std::size_t world, std::uint32_t sector) const {
    CONEY_ASSERT(world < m_worlds.size() && sector < m_worlds[world]->bySector.size());
    return m_worlds[world]->bySector[sector];
}

void WorldSet::setDrawn(std::size_t world, bool drawn) {
    CONEY_ASSERT(world < m_worlds.size());
    m_worlds[world]->drawn = drawn;
}

WorldAtomic* WorldSet::worldAtomic(std::size_t world, std::uint32_t sector) {
    CONEY_ASSERT(world < m_worlds.size() && sector < m_worlds[world]->bySector.size());
    const rw::Atomic* wanted = m_worlds[world]->bySector[sector];
    if (wanted == nullptr) {
        return nullptr;
    }
    // The sector's part holds it among its atomics.
    for (Part& part : m_worlds[world]->parts) {
        for (WorldAtomic& atomic : part.atomics) {
            if (atomic.atomic() == wanted) {
                return &atomic;
            }
        }
    }
    return nullptr;
}

const WorldSet::SectorMesh* WorldSet::sectorMesh(std::size_t world, std::uint32_t sector) const {
    CONEY_ASSERT(world < m_held.size() && sector < m_held[world].meshes.size());
    const std::optional<SectorMesh>& mesh = m_held[world].meshes[sector];
    return mesh ? &*mesh : nullptr;
}

void WorldSet::refreshMesh(std::size_t world, std::uint32_t sector) {
    CONEY_ASSERT(world < m_held.size() && sector < m_held[world].meshes.size());
    std::optional<SectorMesh>& mesh = m_held[world].meshes[sector];
    rw::Atomic* atomic = m_worlds[world]->bySector[sector];
    if (!mesh || atomic == nullptr) {
        return;
    }
    removeMeshes(*mesh);
    mesh = addMeshes(atomic);
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
    CONEY_ASSERT(world < m_worlds.size() && world < m_held.size());
    auto read = readPart(world, part);
    if (!read) {
        dropTextures(m_readTextures);
        return read;
    }
    // Its dictionary's textures went into the scene assets as it was read; its sectors' meshes go in now that their
    // atomics and dual layers are made.
    Held& held = m_held[world];
    held.parts[part - 1] = std::exchange(m_readTextures, {});
    std::size_t borrowed = 0;
    for (const std::uint32_t sector : m_worlds[world]->state.part(part).sectors) {
        if (rw::Atomic* atomic = m_worlds[world]->bySector[sector]; atomic != nullptr) {
            held.meshes[sector] = addMeshes(atomic);
            borrowed += countBorrowed(atomic, world, part) + countBorrowed(dualLayerOf(atomic), world, part);
        }
    }
    // Counted, not warned of: the disc has no such material (renderer.md#texture-lifetime), and one that does appear
    // (a modded file) keeps its texture, as the scene assets hold a texture while a held mesh's material names it.
    m_borrowedTextures += borrowed;
    return read;
}

std::size_t WorldSet::countBorrowed(const rw::Atomic* atomic, std::size_t world, std::uint32_t part) const {
    if (atomic == nullptr || atomic->geometry == nullptr) {
        return 0;
    }
    // Whether `dictionary` is a loaded part's other than this one: the worlds' own dictionaries stay as long as the
    // set, so a texture from one of them cannot go stale under a part.
    const auto otherPart = [&](const rw::TexDictionary* dictionary) {
        for (std::size_t w = 0; w < m_worlds.size(); ++w) {
            const std::vector<Part>& parts = m_worlds[w]->parts;
            for (std::size_t p = 0; p < parts.size(); ++p) {
                const bool self = w == world && p + 1 == part;
                if (!self && parts[p].dictionary && parts[p].dictionary->rwDictionary() == dictionary) {
                    return true;
                }
            }
        }
        return false;
    };
    std::size_t count = 0;
    const rw::Geometry& geometry = *atomic->geometry;
    for (rw::int32 m = 0; m < geometry.matList.numMaterials; ++m) {
        const rw::Texture* texture = geometry.matList.materials[m]->texture;
        if (texture != nullptr && texture->dict != nullptr && otherPart(texture->dict)) {
            ++count;
        }
    }
    return count;
}

void WorldSet::unloadPart(std::size_t world, std::uint32_t part) {
    CONEY_ASSERT(world < m_worlds.size() && world < m_held.size());
    // Out of the scene assets first, while the atomics and the dictionary are still there.
    Held& held = m_held[world];
    for (const std::uint32_t sector : m_worlds[world]->state.part(part).sectors) {
        // Bound once: clang-tidy does not follow the engaged check through a repeated subscript.
        std::optional<SectorMesh>& mesh = held.meshes[sector];
        if (mesh) {
            removeMeshes(*mesh);
            mesh.reset();
        }
    }
    dropTextures(held.parts[part - 1]);
    freePart(world, part);
}

std::expected<void, Error> WorldSet::readPart(std::size_t world, std::uint32_t part) {
    CONEY_ASSERT(world < m_worlds.size());
    Loaded& loaded = *m_worlds[world];
    // Stand-in: no `Sectors<i>` clump is made in the Sector Pool for the part, nor its heap made current around the
    // reads (librw allocates), so the part's size is not measured from the pool: the budget takes the manifest's.
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
    // loaded dictionary). Coney's choice: it stays in the lookup while the part is loaded, where the original makes it
    // current and then none.
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
        // Stand-in: the atomic keeps librw's render callback, which nothing calls (WorldRenderer draws it), where the
        // original swaps in a do-nothing one; and 0x00198190 is not run over its materials (it checks their textures
        // and sets the environment map's frame on each environment-mapped MatFX material: no visible effect yet).
        record.atomics.push_back(std::move(*atomic));
        placed.push_back(entry.streamedIndex);
    }
    for (std::size_t i = 0; i < placed.size(); ++i) {
        loaded.bySector[placed[i]] = record.atomics[i].atomic();
    }
    loaded.parts[part - 1] = std::move(record);
    return {};
}

void WorldSet::freePart(std::size_t world, std::uint32_t part) {
    CONEY_ASSERT(world < m_worlds.size());
    Loaded& loaded = *m_worlds[world];
    // Stand-in: every atomic releases (one thread draws and frees), so the part always frees and nothing is returned,
    // where the original stops and returns 0 at the first sector its release leaves in a state other than 4.
    for (const std::uint32_t sector : loaded.state.part(part).sectors) {
        loaded.bySector[sector] = nullptr;
    }
    // Stand-in: no pool heap to make current or lock (librw allocates), and the world's in-flight part is
    // world::StreamedWorld's to clear.
    // Atomics, then the lookup entry, then the dictionary their materials took textures from.
    Part& record = loaded.parts[part - 1];
    record.atomics.clear();
    record.lookup.reset();
    record.dictionary.reset();
}

} // namespace coney::platform
