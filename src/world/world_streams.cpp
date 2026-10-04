// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_streams.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

#include "fileio/reader.h"
#include "graphics/rw_stream.h"

namespace coney::world {

namespace {

// RenderWare section ids of worlds, atomics and geometry (RenderWare's core ids, as librw's rwbase.h names them).
constexpr std::uint32_t kRwTexture = 0x06;
constexpr std::uint32_t kRwMaterial = 0x07;
constexpr std::uint32_t kRwMaterialList = 0x08;
constexpr std::uint32_t kRwAtomicSector = 0x09;
constexpr std::uint32_t kRwPlaneSector = 0x0A;
constexpr std::uint32_t kRwWorld = 0x0B;
constexpr std::uint32_t kRwGeometry = 0x0F;
constexpr std::uint32_t kRwAtomic = 0x14;
constexpr std::uint32_t kRwRightToRender = 0x1F;
constexpr std::uint32_t kRwMeshPlugin = 0x50E;
constexpr std::uint32_t kRwNativeData = 0x510;

// RenderWare geometry flag: the vertex data is in the platform's own format (here PS2 DMA chains), not in the stream.
constexpr std::uint32_t kGeometryNative = 0x01000000;

// Bytes of fixed structs, RenderWare 3.7 layout.
constexpr std::size_t kWorldStructBytes = 64;        // root flag, origin, counts, format, box
constexpr std::size_t kPlaneSectorStructBytes = 24;  // axis, value, two "is atomic" flags, two extents
constexpr std::size_t kAtomicSectorStructBytes = 44; // material base, counts, two corners, two unused words
constexpr std::size_t kSectorPluginBytes = 20;
constexpr std::size_t kAtomicStructBytes = 16;   // frame index, geometry index, flags, unused
constexpr std::size_t kGeometryStructBytes = 16; // flags, triangles, vertices, morph targets
constexpr std::size_t kMaterialStructBytes = 28; // flags, colour, unused, textured, surface properties

// The longest texture or mask name librw's material reader takes: it reads a name section into a 128-byte array.
constexpr std::uint32_t kMaxTextureNameBytes = 128;

// One section inside a parent: its header, where it starts in the parent, and exactly its data.
struct Section {
    graphics::RwSectionHeader header;
    std::size_t offset = 0;
    std::span<const std::byte> data;
    std::span<const std::byte> whole; // header and data
};

// Reads the next section of the parent `reader` views, checking that it fits. `what` names it in errors.
std::expected<Section, Error> nextSection(io::Reader& reader, std::span<const std::byte> parent,
                                          std::string_view what) {
    const std::size_t offset = reader.position();
    auto header = graphics::readRwSectionHeader(reader);
    if (!header) {
        return fail(ErrorCode::Truncated,
                    std::format("{} at offset {:#x}: the section header is cut off", what, offset));
    }
    auto data = reader.readBytes(header->size);
    if (!data) {
        return fail(ErrorCode::Truncated, std::format("{} at offset {:#x}: section {:#x} holds {} bytes but only {} "
                                                      "remain in its parent",
                                                      what, offset, header->id, header->size, reader.remaining()));
    }
    return Section{*header, offset, *data, parent.subspan(offset, graphics::kRwHeaderSize + header->size)};
}

// Reads the next section and checks its id.
std::expected<Section, Error> expectSection(io::Reader& reader, std::span<const std::byte> parent, std::uint32_t id,
                                            std::string_view what) {
    auto section = nextSection(reader, parent, what);
    if (section && section->header.id != id) {
        return fail(ErrorCode::Invalid, std::format("{} at offset {:#x}: expected section {:#x} but found {:#x}", what,
                                                    section->offset, id, section->header.id));
    }
    return section;
}

// The error for a value read from bytes whose size was checked before: a read cannot fail there.
Error checkedRead() { return Error{ErrorCode::Truncated, "internal: a checked read ran past its data"}; }

// Reads three floats.
std::expected<Vec3, Error> readVec3(io::Reader& reader) {
    auto x = reader.readF32Le();
    auto y = reader.readF32Le();
    auto z = reader.readF32Le();
    if (!x || !y || !z) {
        return std::unexpected(checkedRead());
    }
    return Vec3{*x, *y, *z};
}

// The box spanned by two corners, whichever order they come in.
Box boxOf(Vec3 a, Vec3 b) {
    return Box{Vec3{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)},
               Vec3{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}};
}

// Reads `count` 32-bit words; the caller has checked that they are there.
std::expected<std::vector<std::uint32_t>, Error> readWords(io::Reader& reader, std::size_t count) {
    std::vector<std::uint32_t> words;
    words.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto word = reader.readU32Le();
        if (!word) {
            return std::unexpected(std::move(word.error()));
        }
        words.push_back(*word);
    }
    return words;
}

// Reads one atomic sector: its struct (no triangles allowed) and, from its extension, the sector plugin data.
std::expected<WorldSector, Error> readAtomicSector(const Section& sector) {
    io::Reader reader(sector.data);
    auto info = expectSection(reader, sector.data, graphics::kRwStruct, "atomic sector struct");
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    if (info->data.size() < kAtomicSectorStructBytes) {
        return fail(ErrorCode::Invalid, std::format("atomic sector at {:#x}: struct of {} bytes, expected {}",
                                                    sector.offset, info->data.size(), kAtomicSectorStructBytes));
    }
    io::Reader fields(info->data);
    auto words = readWords(fields, 3); // material list base, triangles, vertices
    if (!words) {
        return std::unexpected(std::move(words.error()));
    }
    if ((*words)[1] != 0 || (*words)[2] != 0) {
        return fail(ErrorCode::Invalid, std::format("atomic sector at {:#x} holds {} triangles and {} vertices; a "
                                                    "streamed world's sectors hold none",
                                                    sector.offset, (*words)[1], (*words)[2]));
    }
    auto a = readVec3(fields);
    auto b = readVec3(fields);
    if (!a || !b) {
        return std::unexpected(checkedRead());
    }
    WorldSector result{boxOf(*a, *b), std::nullopt};

    // The extension: the game's plugin among RenderWare's own (native data, PVS, right to render, ...).
    while (reader.remaining() > 0) {
        auto next = nextSection(reader, sector.data, "atomic sector extension");
        if (!next) {
            return std::unexpected(std::move(next.error()));
        }
        if (next->header.id != graphics::kRwExtension) {
            continue;
        }
        io::Reader plugins(next->data);
        while (plugins.remaining() > 0) {
            auto plugin = nextSection(plugins, next->data, "atomic sector plugin");
            if (!plugin) {
                return std::unexpected(std::move(plugin.error()));
            }
            if (plugin->header.id == kSectorPluginId) {
                auto data = readSectorPluginData(plugin->data);
                if (!data) {
                    return std::unexpected(std::move(data.error()));
                }
                result.plugin = *data;
            }
        }
    }
    return result;
}

// Walks the BSP below `root` (a plane or atomic sector section) left child first, collecting the atomic sectors.
// Iterative, with a stack of sections still to visit; `maxNodes` bounds the walk so damaged data cannot make it loop.
std::expected<void, Error> walkSectors(const Section& root, std::uint32_t maxNodes, WorldStream& world) {
    std::vector<Section> pending{root};
    std::uint32_t visited = 0;
    while (!pending.empty()) {
        const Section node = pending.back();
        pending.pop_back();
        if (++visited > maxNodes) {
            return fail(ErrorCode::Invalid, std::format("the BSP holds more than the {} sectors its world header "
                                                        "counts",
                                                        maxNodes));
        }
        if (node.header.id == kRwAtomicSector) {
            auto sector = readAtomicSector(node);
            if (!sector) {
                return std::unexpected(std::move(sector.error()));
            }
            world.sectors.push_back(*sector);
            continue;
        }
        if (node.header.id != kRwPlaneSector) {
            return fail(ErrorCode::Invalid, std::format("BSP node at {:#x}: section {:#x} is neither a plane nor an "
                                                        "atomic sector",
                                                        node.offset, node.header.id));
        }
        // A plane sector: its struct, then the left and the right child.
        io::Reader reader(node.data);
        auto info = expectSection(reader, node.data, graphics::kRwStruct, "plane sector struct");
        if (!info) {
            return std::unexpected(std::move(info.error()));
        }
        if (info->data.size() != kPlaneSectorStructBytes) {
            return fail(ErrorCode::Invalid, std::format("plane sector at {:#x}: struct of {} bytes, expected {}",
                                                        node.offset, info->data.size(), kPlaneSectorStructBytes));
        }
        auto left = nextSection(reader, node.data, "left child");
        if (!left) {
            return std::unexpected(std::move(left.error()));
        }
        auto right = nextSection(reader, node.data, "right child");
        if (!right) {
            return std::unexpected(std::move(right.error()));
        }
        ++world.planeSectors;
        pending.push_back(*right); // popped after the left subtree
        pending.push_back(*left);
    }
    return {};
}

// Checks a texture section inside a material: a struct, two name sections that fit librw's arrays, an extension.
std::expected<void, Error> checkTexture(const Section& texture) {
    io::Reader reader(texture.data);
    auto info = expectSection(reader, texture.data, graphics::kRwStruct, "texture struct");
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    if (info->data.size() < 4) {
        return fail(ErrorCode::Invalid, "texture struct is shorter than its filter word");
    }
    for (const std::string_view what : {"texture name", "texture mask name"}) {
        auto name = expectSection(reader, texture.data, graphics::kRwString, what);
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        if (name->header.size > kMaxTextureNameBytes) {
            return fail(ErrorCode::Invalid, std::format("{} of {} bytes is longer than librw's {}-byte field", what,
                                                        name->header.size, kMaxTextureNameBytes));
        }
    }
    return {};
}

// Checks the material list section as librw's reader trusts it: a struct with the count and one index per entry (an
// index refers back to an earlier material, -1 means a material section follows), then the material sections.
std::expected<std::uint32_t, Error> checkMaterialList(const Section& list) {
    io::Reader reader(list.data);
    auto info = expectSection(reader, list.data, graphics::kRwStruct, "material list struct");
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    io::Reader fields(info->data);
    auto count = fields.readU32Le();
    if (!count) {
        return fail(ErrorCode::Truncated, "material list struct is shorter than its count");
    }
    if (fields.remaining() / 4 < *count) {
        return fail(ErrorCode::Truncated,
                    std::format("material list of {} entries has room for {} indices", *count, fields.remaining() / 4));
    }
    auto indices = readWords(fields, *count);
    if (!indices) {
        return std::unexpected(std::move(indices.error()));
    }
    for (std::uint32_t i = 0; i < *count; ++i) {
        const auto index = static_cast<std::int32_t>((*indices)[i]);
        if (index >= 0) {
            if (static_cast<std::uint32_t>(index) >= i) {
                return fail(ErrorCode::Invalid, std::format("material {} refers to material {}, which is not before "
                                                            "it",
                                                            i, index));
            }
            continue;
        }
        auto material = expectSection(reader, list.data, kRwMaterial, "material");
        if (!material) {
            return std::unexpected(std::move(material.error()));
        }
        io::Reader parts(material->data);
        auto fieldsSection = expectSection(parts, material->data, graphics::kRwStruct, "material struct");
        if (!fieldsSection) {
            return std::unexpected(std::move(fieldsSection.error()));
        }
        if (fieldsSection->data.size() < kMaterialStructBytes) {
            return fail(ErrorCode::Invalid, std::format("material struct of {} bytes, expected {}",
                                                        fieldsSection->data.size(), kMaterialStructBytes));
        }
        // The fourth word says whether a texture section follows.
        if (io::loadU32Le(fieldsSection->data.subspan(12, 4)) != 0) {
            auto texture = expectSection(parts, material->data, kRwTexture, "material texture");
            if (!texture) {
                return std::unexpected(std::move(texture.error()));
            }
            if (auto checked = checkTexture(*texture); !checked) {
                return std::unexpected(std::move(checked.error()));
            }
        }
    }
    return *count;
}

// Reads the mesh plugin (0x50E) of native geometry: a header and one {indices, material} record per mesh.
std::expected<void, Error> readMeshPlugin(const Section& plugin, AtomicSection& atomic) {
    io::Reader reader(plugin.data);
    auto header = readWords(reader, std::min<std::size_t>(3, reader.remaining() / 4));
    if (!header || header->size() < 3) {
        return fail(ErrorCode::Truncated, "mesh plugin is shorter than its header");
    }
    const std::uint32_t meshCount = (*header)[1];
    const std::uint32_t totalIndices = (*header)[2];
    // librw keeps the mesh count in 16 bits, and native data has no index lists, only the records.
    if (meshCount > 0xFFFF || plugin.data.size() != 12 + std::size_t{meshCount} * 8) {
        return fail(ErrorCode::Invalid, std::format("mesh plugin of {} bytes for {} meshes: native geometry has "
                                                    "exactly 8 bytes a mesh",
                                                    plugin.data.size(), meshCount));
    }
    atomic.triangleStrips = (*header)[0] == 1;
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < meshCount; ++i) {
        auto record = readWords(reader, 2);
        if (!record) {
            return std::unexpected(std::move(record.error()));
        }
        if ((*record)[1] >= atomic.materialCount) {
            return fail(ErrorCode::Invalid,
                        std::format("mesh {} uses material {} of {}", i, (*record)[1], atomic.materialCount));
        }
        atomic.meshes.push_back(MeshInfo{(*record)[0], (*record)[1], {}});
        sum += (*record)[0];
    }
    if (sum != totalIndices) {
        return fail(ErrorCode::Invalid,
                    std::format("mesh plugin totals {} indices but its meshes hold {}", totalIndices, sum));
    }
    return {};
}

// Reads the native data plugin (0x510) of PS2 geometry: a struct with the platform, then per mesh its DMA chain's size,
// a "no reference tags" flag and the chain. The meshes must already be known from the mesh plugin.
std::expected<void, Error> readNativeData(const Section& plugin, AtomicSection& atomic) {
    // The struct header's size is not to be trusted: in the game's files it is larger than the plugin section around it
    // (why is not known). librw skips the header without looking at the size, and so does this: the data is the rest
    // of the plugin section.
    io::Reader outer(plugin.data);
    auto header = graphics::readRwSectionHeader(outer);
    if (!header || header->id != graphics::kRwStruct) {
        return fail(ErrorCode::Invalid, "native data does not start with a struct section");
    }
    io::Reader reader(plugin.data.subspan(graphics::kRwHeaderSize));
    auto platform = reader.readU32Le();
    if (!platform || *platform != kPs2Platform) {
        return fail(ErrorCode::Invalid, "native data is not for the PS2");
    }
    for (std::size_t i = 0; i < atomic.meshes.size(); ++i) {
        auto size = reader.readU32Le();
        auto noRefs = reader.readU32Le();
        if (!size || !noRefs) {
            return fail(ErrorCode::Truncated, std::format("native data ends before mesh {}", i));
        }
        auto chain = reader.readBytes(*size);
        if (!chain) {
            return fail(ErrorCode::Truncated,
                        std::format("native data of mesh {}: {} bytes but {} remain", i, *size, reader.remaining()));
        }
        atomic.meshes[i].nativeData = *chain;
    }
    if (reader.remaining() != 0) {
        return fail(ErrorCode::Invalid,
                    std::format("{} bytes of native data after the last mesh's DMA chain", reader.remaining()));
    }
    return {};
}

// Reads the geometry section's struct, material list and extension into `atomic`.
std::expected<void, Error> readGeometry(const Section& geometry, AtomicSection& atomic) {
    io::Reader reader(geometry.data);
    auto info = expectSection(reader, geometry.data, graphics::kRwStruct, "geometry struct");
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    io::Reader fields(info->data);
    auto words = readWords(fields, std::min<std::size_t>(4, fields.remaining() / 4));
    if (!words || words->size() < 4) {
        return fail(ErrorCode::Truncated, "geometry struct is shorter than its header");
    }
    atomic.geometryFlags = (*words)[0];
    atomic.triangleCount = (*words)[1];
    atomic.vertexCount = (*words)[2];
    const std::uint32_t morphTargets = (*words)[3];
    if ((atomic.geometryFlags & kGeometryNative) == 0) {
        return fail(ErrorCode::Invalid, "the geometry is not PS2 native");
    }
    // Each morph target: a bounding sphere and two flags; native geometry carries no vertex arrays here.
    if (morphTargets != 1 || info->data.size() != kGeometryStructBytes + 24) {
        return fail(ErrorCode::Invalid, std::format("geometry struct of {} bytes with {} morph targets; native "
                                                    "geometry has one and no vertex arrays",
                                                    info->data.size(), morphTargets));
    }
    if (io::loadU32Le(info->data.subspan(32, 4)) != 0 || io::loadU32Le(info->data.subspan(36, 4)) != 0) {
        return fail(ErrorCode::Invalid, "native geometry with vertex arrays in its morph target");
    }

    auto list = expectSection(reader, geometry.data, kRwMaterialList, "material list");
    if (!list) {
        return std::unexpected(std::move(list.error()));
    }
    auto materials = checkMaterialList(*list);
    if (!materials) {
        return std::unexpected(std::move(materials.error()));
    }
    atomic.materialCount = *materials;

    auto extension = expectSection(reader, geometry.data, graphics::kRwExtension, "geometry extension");
    if (!extension) {
        return std::unexpected(std::move(extension.error()));
    }
    // librw's native data reader needs the meshes, so the mesh plugin must come first.
    bool haveMeshes = false;
    bool haveNative = false;
    io::Reader plugins(extension->data);
    while (plugins.remaining() > 0) {
        auto plugin = nextSection(plugins, extension->data, "geometry plugin");
        if (!plugin) {
            return std::unexpected(std::move(plugin.error()));
        }
        if (plugin->header.id == kRwMeshPlugin && !haveMeshes) {
            if (auto read = readMeshPlugin(*plugin, atomic); !read) {
                return std::unexpected(std::move(read.error()));
            }
            haveMeshes = true;
        } else if (plugin->header.id == kRwNativeData && haveMeshes && !haveNative) {
            if (auto read = readNativeData(*plugin, atomic); !read) {
                return std::unexpected(std::move(read.error()));
            }
            haveNative = true;
        } else if (plugin->header.id == kRwMeshPlugin || plugin->header.id == kRwNativeData) {
            return fail(ErrorCode::Invalid, "the geometry's mesh and native data plugins are repeated or out of order");
        }
    }
    if (!haveNative) {
        return fail(ErrorCode::Invalid, "the geometry has no PS2 native data");
    }
    return {};
}

// Reads the atomic's own extension: right to render and the game's atomic plugin.
std::expected<void, Error> readAtomicExtension(const Section& extension, AtomicSection& atomic) {
    io::Reader plugins(extension.data);
    while (plugins.remaining() > 0) {
        auto plugin = nextSection(plugins, extension.data, "atomic plugin");
        if (!plugin) {
            return std::unexpected(std::move(plugin.error()));
        }
        if (plugin->header.id == kRwRightToRender) {
            if (plugin->data.size() != 8) {
                return fail(ErrorCode::Invalid, "the atomic's right to render is not 8 bytes");
            }
            atomic.pipelinePlugin = io::loadU32Le(plugin->data);
            atomic.pipeline = io::loadU32Le(plugin->data.subspan(4));
        } else if (plugin->header.id == kAtomicPluginId) {
            auto data = readAtomicPluginData(plugin->data);
            if (!data) {
                return std::unexpected(std::move(data.error()));
            }
            atomic.plugin = *data;
        }
    }
    return {};
}

} // namespace

bool Box::contains(Vec3 point, float margin) const {
    return point.x >= min.x - margin && point.x <= max.x + margin && point.y >= min.y - margin &&
           point.y <= max.y + margin && point.z >= min.z - margin && point.z <= max.z + margin;
}

std::expected<SectorPluginData, Error> readSectorPluginData(std::span<const std::byte> data) {
    if (data.size() != kSectorPluginBytes) {
        return fail(ErrorCode::Invalid,
                    std::format("sector plugin data of {} bytes, expected {}", data.size(), kSectorPluginBytes));
    }
    io::Reader reader(data);
    SectorPluginData result;
    result.streamedIndex = static_cast<std::int32_t>(reader.readU32Le().value());
    result.part = reader.readU32Le().value();
    result.origin = readVec3(reader).value();
    return result;
}

std::expected<AtomicPluginData, Error> readAtomicPluginData(std::span<const std::byte> data) {
    if (data.size() != kAtomicPluginStreamBytes) {
        return fail(ErrorCode::Invalid,
                    std::format("atomic plugin data of {} bytes, expected {}", data.size(), kAtomicPluginStreamBytes));
    }
    io::Reader reader(data);
    AtomicPluginData result;
    result.positionScale = reader.readF32Le().value();
    result.secondScale = reader.readF32Le().value();
    result.word = reader.readU32Le().value();
    return result;
}

std::expected<WorldStream, Error> inspectWorldStream(std::span<const std::byte> stream) {
    WorldStream world;
    io::Reader reader(stream);
    auto partCount = reader.readU32Le();
    if (!partCount) {
        return fail(ErrorCode::Truncated, "world stream is shorter than its part count");
    }
    world.partCount = *partCount;

    // The texture dictionary, then the world.
    auto dictionary = expectSection(reader, stream, graphics::kRwTexDictionary, "world texture dictionary");
    if (!dictionary) {
        return std::unexpected(std::move(dictionary.error()));
    }
    world.dictionaryOffset = dictionary->offset;
    world.dictionaryBytes = dictionary->whole.size();
    auto rwWorld = expectSection(reader, stream, kRwWorld, "world");
    if (!rwWorld) {
        return std::unexpected(std::move(rwWorld.error()));
    }

    // The world's struct: root flag, origin, triangle and vertex counts, sector counts, format, box.
    io::Reader parts(rwWorld->data);
    auto info = expectSection(parts, rwWorld->data, graphics::kRwStruct, "world struct");
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    if (info->data.size() != kWorldStructBytes) {
        return fail(ErrorCode::Invalid, std::format("world struct of {} bytes, expected the RenderWare 3.7 layout's "
                                                    "{}",
                                                    info->data.size(), kWorldStructBytes));
    }
    io::Reader fields(info->data);
    if (auto skipped = fields.seek(16); !skipped) { // root flag and origin are not needed
        return std::unexpected(checkedRead());
    }
    auto counts = readWords(fields, 6); // triangles, vertices, plane sectors, atomic sectors, collision size, format
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    const std::uint32_t planeCount = (*counts)[2];
    const std::uint32_t sectorCount = (*counts)[3];
    world.worldFormat = (*counts)[5];
    auto a = readVec3(fields);
    auto b = readVec3(fields);
    if (!a || !b) {
        return std::unexpected(checkedRead());
    }
    world.worldBox = boxOf(*a, *b);

    // The material list (empty in streamed worlds; it would be referenced by sector geometry), then the BSP's root.
    if (auto list = expectSection(parts, rwWorld->data, kRwMaterialList, "world material list"); !list) {
        return std::unexpected(std::move(list.error()));
    }
    auto root = nextSection(parts, rwWorld->data, "BSP root");
    if (!root) {
        return std::unexpected(std::move(root.error()));
    }
    if (auto walked = walkSectors(*root, planeCount + sectorCount, world); !walked) {
        return std::unexpected(std::move(walked.error()));
    }
    if (world.planeSectors != planeCount || world.sectors.size() != sectorCount) {
        return fail(ErrorCode::Invalid, std::format("the BSP holds {} plane and {} atomic sectors; its world header "
                                                    "says {} and {}",
                                                    world.planeSectors, world.sectors.size(), planeCount, sectorCount));
    }
    return world;
}

std::expected<PartFile, Error> inspectPartFile(std::span<const std::byte> file) {
    PartFile part;
    io::Reader reader(file);
    auto header = readWords(reader, std::min<std::size_t>(4, reader.remaining() / 4));
    if (!header || header->size() < 4) {
        return fail(ErrorCode::Truncated, "part file is shorter than its 16-byte header");
    }
    if ((*header)[0] != 1 || (*header)[1] != 0 || (*header)[2] != 0) {
        return fail(ErrorCode::Invalid, "part file does not start with the header {1, 0, 0, hash}");
    }
    part.nameHash = (*header)[3];
    auto dictionary = expectSection(reader, file, graphics::kRwTexDictionary, "part texture dictionary");
    if (!dictionary) {
        return std::unexpected(std::move(dictionary.error()));
    }
    part.dictionaryOffset = dictionary->offset;
    part.dictionaryBytes = dictionary->whole.size();
    auto count = reader.readU32Le();
    if (!count) {
        return fail(ErrorCode::Truncated, "part file ends before its atomic count");
    }
    // Each record takes at least an index and a section header, which bounds the count before anything is reserved.
    if (reader.remaining() / (4 + graphics::kRwHeaderSize) < *count) {
        return fail(ErrorCode::Truncated, std::format("part file counts {} atomics but has room for fewer", *count));
    }
    for (std::uint32_t i = 0; i < *count; ++i) {
        auto index = reader.readU32Le();
        if (!index) {
            return fail(ErrorCode::Truncated, std::format("part file ends before atomic {}", i));
        }
        auto atomic = expectSection(reader, file, kRwAtomic, "part atomic");
        if (!atomic) {
            return std::unexpected(std::move(atomic.error()));
        }
        part.atomics.push_back(PartAtomic{*index, atomic->offset, atomic->whole.size()});
    }
    if (reader.remaining() != 0) {
        return fail(ErrorCode::Invalid, std::format("{} bytes after the part file's last atomic", reader.remaining()));
    }
    return part;
}

std::expected<AtomicSection, Error> inspectAtomicSection(std::span<const std::byte> section) {
    io::Reader outer(section);
    auto atomicSection = expectSection(outer, section, kRwAtomic, "atomic");
    if (!atomicSection) {
        return std::unexpected(std::move(atomicSection.error()));
    }
    AtomicSection atomic;
    io::Reader reader(atomicSection->data);
    auto info = expectSection(reader, atomicSection->data, graphics::kRwStruct, "atomic struct");
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    if (info->data.size() != kAtomicStructBytes) {
        return fail(ErrorCode::Invalid,
                    std::format("atomic struct of {} bytes, expected {}", info->data.size(), kAtomicStructBytes));
    }
    // Frame and geometry index mean nothing outside a clump; the game gives each atomic a frame of its own.
    atomic.atomicFlags = io::loadU32Le(info->data.subspan(8, 4));

    // Outside a clump the geometry follows the struct directly.
    auto geometry = expectSection(reader, atomicSection->data, kRwGeometry, "atomic geometry");
    if (!geometry) {
        return std::unexpected(std::move(geometry.error()));
    }
    atomic.geometry = geometry->whole;
    if (auto read = readGeometry(*geometry, atomic); !read) {
        return std::unexpected(std::move(read.error()));
    }
    auto extension = expectSection(reader, atomicSection->data, graphics::kRwExtension, "atomic extension");
    if (!extension) {
        return std::unexpected(std::move(extension.error()));
    }
    atomic.extension = extension->whole;
    if (auto read = readAtomicExtension(*extension, atomic); !read) {
        return std::unexpected(std::move(read.error()));
    }
    return atomic;
}

} // namespace coney::world
