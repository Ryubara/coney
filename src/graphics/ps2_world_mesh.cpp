// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/ps2_world_mesh.h"

#include <format>
#include <optional>
#include <utility>

#include "fileio/reader.h"

namespace coney::graphics {

namespace {

// DMA tag ids (bits 28 to 30 of a source-chain tag), as the PS2's DMA controller defines them.
constexpr std::uint32_t kTagRefe = 0;
constexpr std::uint32_t kTagCnt = 1;
constexpr std::uint32_t kTagRef = 3;
constexpr std::uint32_t kTagRefs = 4;
constexpr std::uint32_t kTagRet = 6;
constexpr std::uint32_t kTagEnd = 7;

// VIF command codes (bits 24 to 30 of a VIF code; bit 31 only asks for an interrupt).
constexpr std::uint32_t kVifNop = 0x00;
constexpr std::uint32_t kVifStcycl = 0x01;
constexpr std::uint32_t kVifOffset = 0x02;
constexpr std::uint32_t kVifBase = 0x03;
constexpr std::uint32_t kVifItop = 0x04;
constexpr std::uint32_t kVifStmod = 0x05;
constexpr std::uint32_t kVifMskpath3 = 0x06;
constexpr std::uint32_t kVifMark = 0x07;
constexpr std::uint32_t kVifFlushe = 0x10;
constexpr std::uint32_t kVifFlush = 0x11;
constexpr std::uint32_t kVifFlusha = 0x13;
constexpr std::uint32_t kVifMscal = 0x14;
constexpr std::uint32_t kVifMscalf = 0x15;
constexpr std::uint32_t kVifMscnt = 0x17;
constexpr std::uint32_t kVifStmask = 0x20;
constexpr std::uint32_t kVifStrow = 0x30;
constexpr std::uint32_t kVifStcol = 0x31;
constexpr std::uint32_t kVifMpg = 0x4A;
constexpr std::uint32_t kVifDirect = 0x50;
constexpr std::uint32_t kVifDirectHl = 0x51;
constexpr std::uint32_t kVifUnpack = 0x60; // 0x60 to 0x7F: bit 4 is the mask flag, bits 0 to 3 the format

// UNPACK formats (vn << 2 | vl) and immediate bits.
constexpr std::uint32_t kV2_16 = 0x05;
constexpr std::uint32_t kV4_16 = 0x0D;
constexpr std::uint32_t kV4_8 = 0x0E;
constexpr std::uint32_t kUnpackMasked = 0x10;
constexpr std::uint32_t kUnpackUnsigned = 0x4000;
constexpr std::uint32_t kUnpackAddressMask = 0x3FF;

// The vertex attributes, by vector-unit slot.
constexpr std::size_t kSlots = 4;
constexpr std::size_t kSlotPosition = 0;
constexpr std::size_t kSlotTexCoords = 1;
constexpr std::size_t kSlotColour = 2;
constexpr std::size_t kSlotNormal = 3;

// The format (and signedness) each slot must have. Texture coordinates come as one set (V2_16) or two (V4_16).
struct SlotFormat {
    std::uint32_t format;
    std::uint32_t alternative;
    bool isUnsigned;
};
constexpr std::array<SlotFormat, kSlots> kSlotFormats{
    {{kV4_16, kV4_16, false}, {kV4_16, kV2_16, false}, {kV4_8, kV4_8, true}, {kV4_8, kV4_8, false}}};

// Gathers the VIF stream a DMA chain sends: for each tag, the two VIF words in its upper half, then its data (inline
// after a cnt or ret tag, elsewhere in the chain for a ref tag). Stops after ret, end or refe.
std::expected<std::vector<std::byte>, Error> gatherVifStream(std::span<const std::byte> chain) {
    std::vector<std::byte> stream;
    std::size_t at = 0;
    for (;;) {
        if (chain.size() < 16 || at > chain.size() - 16) {
            return fail(ErrorCode::Truncated, std::format("DMA chain ends at {:#x} without a ret or end tag", at));
        }
        const std::uint32_t tag = io::loadU32Le(chain.subspan(at, 4));
        const std::uint32_t address = io::loadU32Le(chain.subspan(at + 4, 4));
        const std::size_t bytes = std::size_t{tag & 0xFFFFU} * 16;
        const std::uint32_t id = (tag >> 28) & 7U;
        const auto vifWords = chain.subspan(at + 8, 8);
        stream.insert(stream.end(), vifWords.begin(), vifWords.end());
        std::size_t dataStart = at + 16;
        if (id == kTagRef || id == kTagRefs || id == kTagRefe) {
            dataStart = std::size_t{address} * 16;
        } else if (id != kTagCnt && id != kTagRet && id != kTagEnd) {
            return fail(ErrorCode::Invalid,
                        std::format("DMA tag {:#010x} at {:#x}: id {} is not supported", tag, at, id));
        }
        if (dataStart > chain.size() || bytes > chain.size() - dataStart) {
            return fail(ErrorCode::Truncated, std::format("DMA tag at {:#x}: {} bytes at {:#x} run past the chain's "
                                                          "{} bytes",
                                                          at, bytes, dataStart, chain.size()));
        }
        const auto data = chain.subspan(dataStart, bytes);
        stream.insert(stream.end(), data.begin(), data.end());
        if (id == kTagRet || id == kTagEnd || id == kTagRefe) {
            return stream;
        }
        // A cnt tag's data follows it; the next tag of a ref follows the tag itself.
        at = id == kTagCnt ? at + 16 + bytes : at + 16;
    }
}

// Bytes of an UNPACK's data: `count` vectors of the format, padded to a whole word.
std::size_t unpackBytes(std::uint32_t format, std::uint32_t count) {
    const std::uint32_t components = ((format >> 2) & 3U) + 1;
    const std::uint32_t bits = (format & 3U) == 3 ? 16 : (32U >> (format & 3U)) * components; // V4_5: 16 bits a vector
    return (std::size_t{count} * bits + 31) / 32 * 4;
}

// The unpacks of one batch, by slot, as they come; empty spans for slots not written.
struct Batch {
    std::array<std::span<const std::byte>, kSlots> slots{};
    std::array<std::uint32_t, kSlots> counts{};
    std::uint32_t texCoordSets = 0;
    std::optional<std::uint32_t> itop;
};

// Turns one batch into vertices: checks that ITOP is within every slot's count (each slot is padded on its own, to an
// even count or a whole 16 bytes), then reads the first ITOP vectors of each slot.
std::expected<std::vector<Ps2PackedVertex>, Error> batchVertices(const Batch& batch, std::uint32_t index,
                                                                 Ps2Attributes& attributes) {
    if (batch.slots[kSlotPosition].empty()) {
        return fail(ErrorCode::Invalid, std::format("batch {} has no positions", index));
    }
    if (!batch.itop) {
        return fail(ErrorCode::Invalid, std::format("batch {} has no vertex count (ITOP)", index));
    }
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
        if (!batch.slots[slot].empty() && batch.counts[slot] < *batch.itop) {
            return fail(ErrorCode::Invalid, std::format("batch {}: slot {} holds {} vectors, fewer than its {} "
                                                        "vertices",
                                                        index, slot, batch.counts[slot], *batch.itop));
        }
    }
    const Ps2Attributes here{batch.texCoordSets, !batch.slots[kSlotColour].empty(), !batch.slots[kSlotNormal].empty()};
    if (index == 0) {
        attributes = here;
    } else if (here.texCoordSets != attributes.texCoordSets || here.colours != attributes.colours ||
               here.normals != attributes.normals) {
        return fail(ErrorCode::Invalid, std::format("batch {} carries other attributes than the first", index));
    }
    std::vector<Ps2PackedVertex> vertices(*batch.itop);
    const auto byteAt = [](std::span<const std::byte> data, std::size_t i) {
        return std::to_integer<std::uint8_t>(data[i]);
    };
    // A 16-bit component of a slot's vector `v`, `c`, with `components` to a vector.
    const auto short16 = [&byteAt](std::span<const std::byte> data, std::size_t v, std::size_t c,
                                   std::size_t components) {
        const std::size_t at = (v * components + c) * 2;
        return static_cast<std::int16_t>(byteAt(data, at) | (byteAt(data, at + 1) << 8));
    };
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        for (std::size_t c = 0; c < 4; ++c) {
            vertices[v].position[c] = short16(batch.slots[kSlotPosition], v, c, 4);
            if (c < std::size_t{here.texCoordSets} * 2) {
                vertices[v].texCoords[c] =
                    short16(batch.slots[kSlotTexCoords], v, c, std::size_t{here.texCoordSets} * 2);
            }
            if (here.colours) {
                vertices[v].colour[c] = byteAt(batch.slots[kSlotColour], v * 4 + c);
            }
            if (here.normals) {
                vertices[v].normal[c] = static_cast<std::int8_t>(byteAt(batch.slots[kSlotNormal], v * 4 + c));
            }
        }
    }
    return vertices;
}

// Appends a batch's vertices to the mesh; a strip batch after the first starts with the previous batch's last two.
std::expected<void, Error> joinBatch(std::vector<Ps2PackedVertex>&& vertices, bool triangleStrip, std::uint32_t index,
                                     Ps2WorldMesh& mesh) {
    std::size_t skip = 0;
    if (triangleStrip && index > 0) {
        const std::size_t have = mesh.vertices.size();
        if (vertices.size() < 2 || have < 2 || vertices[0] != mesh.vertices[have - 2] ||
            vertices[1] != mesh.vertices[have - 1]) {
            return fail(
                ErrorCode::Invalid,
                std::format("strip batch {} does not start with the last two vertices of batch {}", index, index - 1));
        }
        skip = 2;
    }
    mesh.vertices.insert(mesh.vertices.end(), vertices.begin() + static_cast<std::ptrdiff_t>(skip), vertices.end());
    return {};
}

} // namespace

std::expected<Ps2WorldMesh, Error> decodePs2WorldMesh(std::span<const std::byte> chain, bool triangleStrip) {
    auto gathered = gatherVifStream(chain);
    if (!gathered) {
        return std::unexpected(std::move(gathered.error()));
    }
    const std::span<const std::byte> stream(*gathered);
    io::Reader reader(stream);
    Ps2WorldMesh mesh;
    Batch batch;
    std::uint32_t cycle = 0;
    std::uint32_t write = 0;
    while (reader.remaining() >= 4) {
        const std::size_t at = reader.position();
        const std::uint32_t code = reader.readU32Le().value();
        const std::uint32_t command = (code >> 24) & 0x7FU;
        const std::uint32_t number = (code >> 16) & 0xFFU;
        const std::uint32_t immediate = code & 0xFFFFU;
        // Skips a command's `bytes` of data.
        const auto skip = [&](std::size_t bytes) -> std::expected<void, Error> {
            if (!reader.readBytes(bytes)) {
                return fail(ErrorCode::Truncated, std::format("VIF command {:#010x} at {:#x}: its {} bytes of data "
                                                              "are cut off",
                                                              code, at, bytes));
            }
            return {};
        };
        std::expected<void, Error> done;
        if (command >= kVifUnpack) {
            // An UNPACK: data for one slot of the batch.
            const std::uint32_t format = command & 0x0FU;
            const std::uint32_t count = number == 0 ? 256 : number;
            const std::size_t slot = immediate & kUnpackAddressMask;
            if ((command & kUnpackMasked) != 0 || cycle != kSlots || write != 1 || slot >= kSlots ||
                (format != kSlotFormats[slot].format && format != kSlotFormats[slot].alternative) ||
                ((immediate & kUnpackUnsigned) != 0) != kSlotFormats[slot].isUnsigned || !batch.slots[slot].empty()) {
                return fail(ErrorCode::Invalid, std::format("UNPACK {:#010x} at {:#x} (cycle {}, {}) is not the world "
                                                            "vertex layout",
                                                            code, at, cycle, write));
            }
            auto data = reader.readBytes(unpackBytes(format, count));
            if (!data) {
                return fail(ErrorCode::Truncated,
                            std::format("UNPACK {:#010x} at {:#x}: its data is cut off", code, at));
            }
            batch.slots[slot] = *data;
            batch.counts[slot] = count;
            if (slot == kSlotTexCoords) {
                batch.texCoordSets = format == kV4_16 ? 2 : 1;
            }
            continue;
        }
        switch (command) {
        case kVifNop:
        case kVifOffset:
        case kVifBase:
        case kVifMskpath3:
        case kVifMark:
        case kVifFlushe:
        case kVifFlush:
        case kVifFlusha:
            break;
        case kVifStcycl:
            cycle = immediate & 0xFFU;
            write = (immediate >> 8) & 0xFFU;
            break;
        case kVifStmod:
            if ((immediate & 3U) != 0) {
                return fail(ErrorCode::Invalid, std::format("STMOD {} at {:#x}: offset or difference unpacking is not "
                                                            "the world vertex layout",
                                                            immediate & 3U, at));
            }
            break;
        case kVifItop:
            batch.itop = immediate & kUnpackAddressMask;
            break;
        case kVifMscal:
        case kVifMscalf:
        case kVifMscnt: {
            // The microprogram runs on the batch: it is complete.
            auto vertices = batchVertices(batch, mesh.batches, mesh.attributes);
            if (!vertices) {
                return std::unexpected(std::move(vertices.error()));
            }
            if (auto joined = joinBatch(std::move(*vertices), triangleStrip, mesh.batches, mesh); !joined) {
                return std::unexpected(std::move(joined.error()));
            }
            ++mesh.batches;
            batch = Batch{};
            break;
        }
        case kVifStmask:
            done = skip(4);
            break;
        case kVifStrow:
        case kVifStcol:
            done = skip(16);
            break;
        case kVifMpg:
            done = skip(std::size_t{number == 0 ? 256U : number} * 8);
            break;
        case kVifDirect:
        case kVifDirectHl:
            done = skip(std::size_t{immediate == 0 ? 65536U : immediate} * 16);
            break;
        default:
            return fail(ErrorCode::Invalid, std::format("VIF command {:#010x} at {:#x} is not supported", code, at));
        }
        if (!done) {
            return std::unexpected(std::move(done.error()));
        }
    }
    if (!batch.slots[kSlotPosition].empty() || mesh.batches == 0) {
        return fail(ErrorCode::Invalid, "the chain ends with vertices no microprogram call draws");
    }
    return mesh;
}

} // namespace coney::graphics
