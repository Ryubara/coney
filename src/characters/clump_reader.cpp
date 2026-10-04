// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/clump_reader.h"

#include <bit>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include "fileio/reader.h"
#include "graphics/rw_stream.h"

namespace coney::characters {

namespace {

// Bytes of one frame record in the frame list's struct, and of one inverse bind matrix.
constexpr std::size_t kFrameRecordBytes = 56;
constexpr std::size_t kMatrixBytes = 64;
// Bytes of one HAnim node record, and of the HAnim extension's header (version, id, node count).
constexpr std::size_t kHAnimNodeBytes = 12;
constexpr std::size_t kHAnimHeaderBytes = 12;
// The PS2 platform id of native geometry and native skins.
constexpr std::uint32_t kPs2 = 4;

// A section: its id and its data (header stripped).
struct Section {
    std::uint32_t id = 0;
    std::span<const std::byte> body;
};

// Reads the section header at `at` of `data` and moves `at` past the section. Fails when the header or the data runs
// past `data`.
std::expected<Section, Error> nextSection(std::span<const std::byte> data, std::size_t& at) {
    if (data.size() - at < graphics::kRwHeaderSize) {
        return fail(ErrorCode::Truncated, std::format("a section header at {:#x} is cut off", at));
    }
    const std::uint32_t id = io::loadU32Le(data.subspan(at, 4));
    const std::uint32_t size = io::loadU32Le(data.subspan(at + 4, 4));
    const std::size_t body = at + graphics::kRwHeaderSize;
    if (size > data.size() - body) {
        return fail(ErrorCode::Truncated,
                    std::format("section {:#x} at {:#x} holds {} bytes, more than its parent's {} left", id, at, size,
                                data.size() - body));
    }
    at = body + size;
    return Section{id, data.subspan(body, size)};
}

// Reads the next section, which must have `id`; `what` names it for the message.
std::expected<Section, Error> expectSection(std::span<const std::byte> data, std::size_t& at, std::uint32_t id,
                                            std::string_view what) {
    auto section = nextSection(data, at);
    if (!section) {
        return section;
    }
    if (section->id != id) {
        return fail(ErrorCode::Invalid,
                    std::format("expected the {} section ({:#x}) but found {:#x}", what, id, section->id));
    }
    return section;
}

// Every section inside `body`, in order: the plugin sections of an extension.
std::expected<std::vector<Section>, Error> childSections(std::span<const std::byte> body) {
    std::vector<Section> sections;
    std::size_t at = 0;
    while (at < body.size()) {
        auto section = nextSection(body, at);
        if (!section) {
            return std::unexpected(std::move(section.error()));
        }
        sections.push_back(*section);
    }
    return sections;
}

// The float at `at` of `bytes`, which the caller has checked holds it.
float floatAt(std::span<const std::byte> bytes, std::size_t at) {
    return std::bit_cast<float>(io::loadU32Le(bytes.subspan(at, 4)));
}

// The vector of three floats at `at`.
anim::Vec3 vecAt(std::span<const std::byte> bytes, std::size_t at) {
    return anim::Vec3{floatAt(bytes, at), floatAt(bytes, at + 4), floatAt(bytes, at + 8)};
}

// The text of a string section: up to its first zero byte.
std::string stringOf(std::span<const std::byte> body) {
    std::string text;
    for (const std::byte c : body) {
        if (c == std::byte{0}) {
            break;
        }
        text.push_back(static_cast<char>(c));
    }
    return text;
}

// Reads a frame's HAnim extension into the frame and, when it carries the node list, into `hierarchy`.
std::expected<void, Error> readHAnim(std::span<const std::byte> body, ClumpFrame& frame,
                                     std::vector<HAnimNode>& hierarchy) {
    if (body.size() < kHAnimHeaderBytes) {
        return fail(ErrorCode::Truncated, "an HAnim extension is shorter than its header");
    }
    frame.hanimId = static_cast<std::int32_t>(io::loadU32Le(body.subspan(4, 4)));
    const std::uint32_t nodes = io::loadU32Le(body.subspan(8, 4));
    if (nodes == 0) {
        return {};
    }
    // Flags and key size, then the nodes.
    if (body.size() < kHAnimHeaderBytes + 8 || (body.size() - kHAnimHeaderBytes - 8) / kHAnimNodeBytes < nodes) {
        return fail(ErrorCode::Truncated, std::format("an HAnim hierarchy of {} nodes is cut off", nodes));
    }
    if (!hierarchy.empty()) {
        return fail(ErrorCode::Invalid, "the clump has two HAnim hierarchies");
    }
    for (std::uint32_t i = 0; i < nodes; ++i) {
        const std::size_t at = kHAnimHeaderBytes + 8 + i * kHAnimNodeBytes;
        hierarchy.push_back(HAnimNode{static_cast<std::int32_t>(io::loadU32Le(body.subspan(at, 4))),
                                      static_cast<std::int32_t>(io::loadU32Le(body.subspan(at + 4, 4))),
                                      io::loadU32Le(body.subspan(at + 8, 4))});
    }
    return {};
}

// Reads the frame list: the struct of frame records, then one extension per frame.
std::expected<void, Error> readFrameList(std::span<const std::byte> body, ClumpData& clump) {
    std::size_t at = 0;
    auto structure = expectSection(body, at, graphics::kRwStruct, "frame list struct");
    if (!structure) {
        return std::unexpected(std::move(structure.error()));
    }
    const std::span<const std::byte> records = structure->body;
    if (records.size() < 4) {
        return fail(ErrorCode::Truncated, "the frame list's struct is empty");
    }
    const std::uint32_t count = io::loadU32Le(records.first(4));
    if ((records.size() - 4) / kFrameRecordBytes < count) {
        return fail(ErrorCode::Truncated, std::format("the frame list's {} frames are cut off", count));
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t r = 4 + i * kFrameRecordBytes;
        ClumpFrame frame;
        frame.matrix =
            anim::Mat34{vecAt(records, r), vecAt(records, r + 12), vecAt(records, r + 24), vecAt(records, r + 36)};
        frame.parent = static_cast<std::int32_t>(io::loadU32Le(records.subspan(r + 48, 4)));
        if (frame.parent >= static_cast<std::int32_t>(i)) {
            return fail(ErrorCode::Invalid,
                        std::format("frame {} names parent {}, not an earlier frame", i, frame.parent));
        }
        clump.frames.push_back(frame);
    }
    for (ClumpFrame& frame : clump.frames) {
        auto extension = expectSection(body, at, graphics::kRwExtension, "frame extension");
        if (!extension) {
            return std::unexpected(std::move(extension.error()));
        }
        auto plugins = childSections(extension->body);
        if (!plugins) {
            return std::unexpected(std::move(plugins.error()));
        }
        for (const Section& plugin : *plugins) {
            if (plugin.id == kRwHAnim) {
                if (auto read = readHAnim(plugin.body, frame, clump.hierarchy); !read) {
                    return read;
                }
            }
        }
    }
    return {};
}

// Reads one material: its struct (flags, colour, unused, textured, three lighting floats), then its texture.
std::expected<ClumpMaterial, Error> readMaterial(std::span<const std::byte> body) {
    std::size_t at = 0;
    auto structure = expectSection(body, at, graphics::kRwStruct, "material struct");
    if (!structure) {
        return std::unexpected(std::move(structure.error()));
    }
    if (structure->body.size() < 16) {
        return fail(ErrorCode::Truncated, "a material struct is cut off");
    }
    ClumpMaterial material;
    for (std::size_t c = 0; c < 4; ++c) {
        material.colour[c] = std::to_integer<std::uint8_t>(structure->body[4 + c]);
    }
    if (io::loadU32Le(structure->body.subspan(12, 4)) == 0) {
        return material; // untextured
    }
    auto texture = expectSection(body, at, kRwTexture, "texture");
    if (!texture) {
        return std::unexpected(std::move(texture.error()));
    }
    std::size_t t = 0;
    auto filter = expectSection(texture->body, t, graphics::kRwStruct, "texture struct");
    if (!filter) {
        return std::unexpected(std::move(filter.error()));
    }
    auto name = expectSection(texture->body, t, graphics::kRwString, "texture name");
    if (!name) {
        return std::unexpected(std::move(name.error()));
    }
    if (filter->body.size() >= 4) {
        material.textureFilter = io::loadU32Le(filter->body.first(4));
    }
    material.texture = stringOf(name->body);
    return material;
}

// Reads the material list: the struct (count, then -1 for each material written out), then the materials.
std::expected<void, Error> readMaterialList(std::span<const std::byte> body, ClumpData& clump) {
    std::size_t at = 0;
    auto structure = expectSection(body, at, graphics::kRwStruct, "material list struct");
    if (!structure) {
        return std::unexpected(std::move(structure.error()));
    }
    if (structure->body.size() < 4) {
        return fail(ErrorCode::Truncated, "the material list's struct is empty");
    }
    const std::uint32_t count = io::loadU32Le(structure->body.first(4));
    if ((structure->body.size() - 4) / 4 < count) {
        return fail(ErrorCode::Truncated, std::format("the material list's {} entries are cut off", count));
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (io::loadU32Le(structure->body.subspan(4 + i * 4, 4)) != 0xFFFFFFFF) {
            return fail(ErrorCode::Invalid, std::format("material {} refers to another one; not supported", i));
        }
        auto section = expectSection(body, at, kRwMaterial, "material");
        if (!section) {
            return std::unexpected(std::move(section.error()));
        }
        auto material = readMaterial(section->body);
        if (!material) {
            return std::unexpected(std::move(material.error()));
        }
        clump.materials.push_back(std::move(*material));
    }
    return {};
}

// Reads the mesh plugin (flags, mesh count, total; per mesh its count and material) and the native data (platform,
// then per mesh a size, a flag and its DMA chain). The native data's struct header gives a size larger than its
// section (as in the world's atomics, docs/research/world.md#part-file), so it is skipped unread, as RenderWare does.
std::expected<void, Error> readMeshes(std::span<const std::byte> binMesh, std::span<const std::byte> native,
                                      ClumpData& clump) {
    if (binMesh.size() < 12) {
        return fail(ErrorCode::Truncated, "the mesh plugin is cut off");
    }
    clump.triangleStrips = io::loadU32Le(binMesh.first(4)) == 1;
    const std::uint32_t count = io::loadU32Le(binMesh.subspan(4, 4));
    if ((binMesh.size() - 12) / 8 < count) {
        return fail(ErrorCode::Truncated, std::format("the mesh plugin's {} meshes are cut off", count));
    }
    if (native.size() < graphics::kRwHeaderSize + 4 ||
        io::loadU32Le(native.subspan(graphics::kRwHeaderSize, 4)) != kPs2) {
        return fail(ErrorCode::Invalid, "the native data is not PS2 geometry");
    }
    std::size_t at = graphics::kRwHeaderSize + 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        ClumpMesh mesh;
        mesh.indexCount = io::loadU32Le(binMesh.subspan(12 + i * 8, 4));
        mesh.materialIndex = io::loadU32Le(binMesh.subspan(16 + i * 8, 4));
        if (mesh.materialIndex >= clump.materials.size()) {
            return fail(ErrorCode::Invalid,
                        std::format("mesh {} names material {} of {}", i, mesh.materialIndex, clump.materials.size()));
        }
        if (native.size() - at < 8) {
            return fail(ErrorCode::Truncated, std::format("the native data of mesh {} is cut off", i));
        }
        const std::uint32_t size = io::loadU32Le(native.subspan(at, 4));
        at += 8;
        if (size > native.size() - at) {
            return fail(ErrorCode::Truncated, std::format("mesh {}'s {} bytes of native data are cut off", i, size));
        }
        mesh.nativeData = native.subspan(at, size);
        at += size;
        clump.meshes.push_back(mesh);
    }
    return {};
}

// Reads the PS2 skin: platform, bone count, used-bone count, most weights a vertex, the used bones and one inverse
// matrix per bone (16 floats, RenderWare's right, up, at, pos with a fourth word each). What follows (16 bytes and
// the split data) is not used.
std::expected<void, Error> readSkin(std::span<const std::byte> body, ClumpData& clump) {
    std::size_t at = 0;
    auto structure = expectSection(body, at, graphics::kRwStruct, "skin struct");
    if (!structure) {
        return std::unexpected(std::move(structure.error()));
    }
    const std::span<const std::byte> data = structure->body;
    if (data.size() < 8 || io::loadU32Le(data.first(4)) != kPs2) {
        return fail(ErrorCode::Invalid, "the skin is not a PS2 native skin");
    }
    ClumpSkin& skin = clump.skin;
    skin.boneCount = std::to_integer<std::uint32_t>(data[4]);
    const auto used = std::to_integer<std::size_t>(data[5]);
    skin.maxWeights = std::to_integer<std::uint32_t>(data[6]);
    if (data.size() < 8 + used + skin.boneCount * kMatrixBytes) {
        return fail(ErrorCode::Truncated, std::format("the skin's {} bones are cut off", skin.boneCount));
    }
    for (std::size_t i = 0; i < used; ++i) {
        skin.usedBones.push_back(std::to_integer<std::uint8_t>(data[8 + i]));
    }
    for (std::uint32_t b = 0; b < skin.boneCount; ++b) {
        const std::size_t m = 8 + used + b * kMatrixBytes;
        skin.inverseBind.push_back(
            anim::Mat34{vecAt(data, m), vecAt(data, m + 16), vecAt(data, m + 32), vecAt(data, m + 48)});
    }
    return {};
}

// Reads the geometry: struct (format, triangles, vertices, morph targets), the material list, then the extension
// with the mesh plugin, the native data and the skin.
std::expected<void, Error> readGeometry(std::span<const std::byte> body, ClumpData& clump) {
    std::size_t at = 0;
    auto structure = expectSection(body, at, graphics::kRwStruct, "geometry struct");
    if (!structure) {
        return std::unexpected(std::move(structure.error()));
    }
    if (structure->body.size() < 16) {
        return fail(ErrorCode::Truncated, "the geometry struct is cut off");
    }
    clump.geometryFlags = io::loadU32Le(structure->body.first(4));
    clump.triangleCount = io::loadU32Le(structure->body.subspan(4, 4));
    clump.vertexCount = io::loadU32Le(structure->body.subspan(8, 4));
    constexpr std::uint32_t kNativeBit = 0x01000000;
    if ((clump.geometryFlags & kNativeBit) == 0) {
        return fail(ErrorCode::Invalid, "the geometry is not native: the characters' are all PS2 native");
    }
    auto materials = expectSection(body, at, kRwMaterialList, "material list");
    if (!materials) {
        return std::unexpected(std::move(materials.error()));
    }
    if (auto read = readMaterialList(materials->body, clump); !read) {
        return read;
    }
    auto extension = expectSection(body, at, graphics::kRwExtension, "geometry extension");
    if (!extension) {
        return std::unexpected(std::move(extension.error()));
    }
    auto plugins = childSections(extension->body);
    if (!plugins) {
        return std::unexpected(std::move(plugins.error()));
    }
    std::optional<std::span<const std::byte>> binMesh;
    std::optional<std::span<const std::byte>> native;
    bool skinned = false;
    for (const Section& plugin : *plugins) {
        if (plugin.id == kRwBinMesh) {
            binMesh = plugin.body;
        } else if (plugin.id == kRwNativeData) {
            native = plugin.body;
        } else if (plugin.id == kRwSkin) {
            if (auto read = readSkin(plugin.body, clump); !read) {
                return read;
            }
            skinned = true;
        }
    }
    if (!binMesh || !native || !skinned) {
        return fail(ErrorCode::Invalid, "the geometry lacks its mesh plugin, native data or skin");
    }
    return readMeshes(*binMesh, *native, clump);
}

// Reads the atomic: struct (frame, geometry, flags, unused), then its extension with the right to render and the
// game's atomic plugin.
std::expected<void, Error> readAtomic(std::span<const std::byte> body, ClumpData& clump) {
    std::size_t at = 0;
    auto structure = expectSection(body, at, graphics::kRwStruct, "atomic struct");
    if (!structure) {
        return std::unexpected(std::move(structure.error()));
    }
    if (structure->body.size() < 8) {
        return fail(ErrorCode::Truncated, "the atomic struct is cut off");
    }
    clump.atomicFrame = io::loadU32Le(structure->body.first(4));
    if (clump.atomicFrame >= clump.frames.size() || io::loadU32Le(structure->body.subspan(4, 4)) != 0) {
        return fail(ErrorCode::Invalid, "the atomic names a frame or geometry the clump does not have");
    }
    auto extension = expectSection(body, at, graphics::kRwExtension, "atomic extension");
    if (!extension) {
        return std::unexpected(std::move(extension.error()));
    }
    auto plugins = childSections(extension->body);
    if (!plugins) {
        return std::unexpected(std::move(plugins.error()));
    }
    for (const Section& plugin : *plugins) {
        if (plugin.id == kRwRightToRender && plugin.body.size() >= 8) {
            clump.atomicPipeline = io::loadU32Le(plugin.body.subspan(4, 4));
        } else if (plugin.id == world::kAtomicPluginId) {
            auto scales = world::readAtomicPluginData(plugin.body);
            if (!scales) {
                return std::unexpected(std::move(scales.error()));
            }
            clump.scales = *scales;
        }
    }
    return {};
}

} // namespace

std::expected<ClumpData, Error> readCharacterClump(std::span<const std::byte> stream) {
    std::size_t at = 0;
    auto clumpSection = expectSection(stream, at, kRwClump, "clump");
    if (!clumpSection) {
        return std::unexpected(std::move(clumpSection.error()));
    }
    const std::span<const std::byte> body = clumpSection->body;
    std::size_t b = 0;

    // The struct: one atomic, no lights or cameras.
    auto structure = expectSection(body, b, graphics::kRwStruct, "clump struct");
    if (!structure) {
        return std::unexpected(std::move(structure.error()));
    }
    if (structure->body.size() < 4 || io::loadU32Le(structure->body.first(4)) != 1) {
        return fail(ErrorCode::Invalid, "the clump does not hold exactly one atomic");
    }
    ClumpData clump;

    // The frames, then the one geometry, then the atomic.
    auto frames = expectSection(body, b, kRwFrameList, "frame list");
    if (!frames) {
        return std::unexpected(std::move(frames.error()));
    }
    if (auto read = readFrameList(frames->body, clump); !read) {
        return std::unexpected(std::move(read.error()));
    }
    auto geometries = expectSection(body, b, kRwGeometryList, "geometry list");
    if (!geometries) {
        return std::unexpected(std::move(geometries.error()));
    }
    std::size_t g = 0;
    auto geometryCount = expectSection(geometries->body, g, graphics::kRwStruct, "geometry list struct");
    if (!geometryCount) {
        return std::unexpected(std::move(geometryCount.error()));
    }
    if (geometryCount->body.size() < 4 || io::loadU32Le(geometryCount->body.first(4)) != 1) {
        return fail(ErrorCode::Invalid, "the clump does not hold exactly one geometry");
    }
    auto geometry = expectSection(geometries->body, g, kRwGeometry, "geometry");
    if (!geometry) {
        return std::unexpected(std::move(geometry.error()));
    }
    if (auto read = readGeometry(geometry->body, clump); !read) {
        return std::unexpected(std::move(read.error()));
    }
    auto atomic = expectSection(body, b, kRwAtomic, "atomic");
    if (!atomic) {
        return std::unexpected(std::move(atomic.error()));
    }
    if (auto read = readAtomic(atomic->body, clump); !read) {
        return std::unexpected(std::move(read.error()));
    }
    if (clump.hierarchy.empty() || clump.skin.boneCount != clump.hierarchy.size()) {
        return fail(ErrorCode::Invalid, std::format("the skin's {} bones do not match the HAnim hierarchy's {} nodes",
                                                    clump.skin.boneCount, clump.hierarchy.size()));
    }
    return clump;
}

} // namespace coney::characters
