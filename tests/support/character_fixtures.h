// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic character files, built byte by byte in the tests. Nothing here comes from the game (LEGAL.md, "No game
// data"). The layouts follow docs/research/characters.md and docs/research/formats/animation.md, and RenderWare's
// binary stream format as librw reads it.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "graphics/ps2_world_mesh.h"
#include "support/fixtures.h"
#include "support/world_fixtures.h"

namespace coney::test {

/// A packed skinned vertex: ps2Vertex()'s attributes, with weight `weight` on bone `bone` and the rest on bone
/// `otherBone` (when `weight` is below 1). Bones are stored as the characters store them: (bone + 1) << 2 in the
/// weight's low bits, 0 for an unused slot.
inline graphics::Ps2PackedVertex skinnedVertex(std::int16_t x, std::int16_t y, std::int16_t z, std::uint32_t bone,
                                               float weight = 1.0F, std::uint32_t otherBone = 0) {
    graphics::Ps2PackedVertex v = ps2Vertex(x, y, z);
    const auto pack = [](float w, std::uint32_t b) {
        return (std::bit_cast<std::uint32_t>(w) & ~0x3FFU) | (b + 1) << 2;
    };
    v.skin[0] = pack(weight, bone);
    if (weight < 1.0F) {
        v.skin[1] = pack(1.0F - weight, otherBone);
    }
    return v;
}

/// A skinned mesh's DMA chain, everything inline after one cnt tag: each batch is STCYCL 5, 1 and five UNPACKs (the
/// world's four slots and the weights as V4_32 in slot 4), unpacked with an even vector count, then ITOP and a
/// microprogram start, as the characters' chains are laid out.
inline Bytes ps2SkinnedMeshChain(std::initializer_list<std::vector<graphics::Ps2PackedVertex>> batches) {
    constexpr std::array<std::uint32_t, 5> kUnpacks{0x6D008000, 0x6D008001, 0x6E00C002, 0x6E008003, 0x6C008004};
    constexpr std::uint32_t kStcycl = 0x01000105; // cycle 5, write 1
    Bytes stream;
    bool first = true;
    for (const auto& batch : batches) {
        const std::size_t count = (batch.size() + 1) / 2 * 2;
        std::vector<Bytes> slots = ps2BatchSlots(batch, count);
        Bytes weights;
        for (std::size_t i = 0; i < count; ++i) {
            const graphics::Ps2PackedVertex v = i < batch.size() ? batch[i] : graphics::Ps2PackedVertex{};
            for (const std::uint32_t word : v.skin) {
                weights.u32(word);
            }
        }
        slots.push_back(weights);
        for (std::size_t s = 0; s < slots.size(); ++s) {
            stream.u32(kStcycl).u32(kUnpacks[s] | static_cast<std::uint32_t>(count) << 16).append(slots[s].span());
        }
        stream.u32(0x04000000 | static_cast<std::uint32_t>(batch.size()));
        stream.u32(first ? 0x15000000 : 0x17000000); // MSCALF, then MSCNT
        first = false;
    }
    stream.padTo((stream.size() + 15) / 16 * 16);
    Bytes chain;
    chain.u32(0x10000000 | static_cast<std::uint32_t>(stream.size() / 16)).u32(0).u32(0).u32(0);
    chain.append(stream.span());
    chain.u32(0x60000000).u32(0).u32(0x11000000).u32(0x11000000); // ret; FLUSH, FLUSH
    return chain;
}

} // namespace coney::test
