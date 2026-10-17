#pragma once

// Builders for synthetic chunk containers, shared by the chunk container suites.

#include "formats/chunk_container.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace coney::test {

using formats::kPackHash;

using Buffer = std::vector<std::byte>;

inline void put_u32(Buffer& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
    }
}

struct ChunkSpec {
    std::uint32_t type;
    std::uint32_t hash;
    std::size_t size;
    std::uint8_t fill = 0xAB;
};

inline Buffer make_resource(std::uint32_t hash, const std::vector<ChunkSpec>& chunks) {
    std::uint32_t sum = 0;
    for (const auto& c : chunks) {
        sum += static_cast<std::uint32_t>(c.size);
    }
    Buffer out;
    put_u32(out, static_cast<std::uint32_t>(chunks.size()));
    put_u32(out, sum);
    put_u32(out, 0);
    put_u32(out, hash);
    for (const auto& c : chunks) {
        put_u32(out, c.type);
        put_u32(out, static_cast<std::uint32_t>(c.size));
        put_u32(out, 0);
        put_u32(out, c.hash);
        out.insert(out.end(), c.size, static_cast<std::byte>(c.fill));
    }
    return out;
}

inline Buffer make_pack(const std::vector<Buffer>& resources) {
    Buffer body;
    for (const auto& r : resources) {
        body.insert(body.end(), r.begin(), r.end());
    }
    Buffer out;
    put_u32(out, static_cast<std::uint32_t>(resources.size()));
    put_u32(out, static_cast<std::uint32_t>(body.size()));
    put_u32(out, 0);
    put_u32(out, kPackHash);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline void poke_u32(Buffer& b, std::size_t offset, std::uint32_t value) {
    Buffer tmp;
    put_u32(tmp, value);
    std::memcpy(b.data() + offset, tmp.data(), 4);
}

inline Buffer sample_flat() {
    return make_resource(0x11223344, {{formats::chunk_type::kAnimationData, 0x11223344, 32},
                                      {formats::chunk_type::kClipDescriptor, 0x55, 80, 0x02},
                                      {formats::chunk_type::kTextureDictionary, 0x66, 0, 0}});
}


// Reads the u32 at `offset` of `b`.
inline std::uint32_t peek_u32(const Buffer& b, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(b[offset + i]) << (8 * i);
    }
    return value;
}

} // namespace coney::test
