// Corrupts valid synthetic containers in every way a loop can reach and checks that the reader either rejects the
// result or returns views that stay inside the input. Run under the sanitized build, an out-of-bounds read fails too.

#include "chunk_builders.hpp"
#include "formats/chunk_container.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

using namespace coney::formats;
using namespace coney::test;

namespace {

// The u32 values tried in every header field: limits, alignment edges and sign edges.
constexpr std::array kEdgeValues{0x00000000u, 0x00000001u, 0x0000000Fu, 0x00000010u, 0x00000053u, 0x00000054u,
                                 0x7FFFFFF0u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFF0u, 0xFFFFFFFFu, kPackHash};

bool inside(std::span<const std::byte> part, std::span<const std::byte> whole) {
    return part.data() >= whole.data() && part.data() + part.size() <= whole.data() + whole.size();
}

// A parsed resource is sound when its chunks follow each other inside the input as the layout says: each payload
// directly after its 16-byte header, each header directly after the previous payload, with sizes that are multiples
// of 16 and types in range. Returns the byte after the last payload.
const std::byte* check_resource(const Resource& resource, std::span<const std::byte> whole) {
    REQUIRE_FALSE(resource.chunks.empty());
    const std::byte* expected = resource.chunks.front().payload.data();
    for (const Chunk& chunk : resource.chunks) {
        REQUIRE(inside(chunk.payload, whole));
        REQUIRE(chunk.payload.data() == expected);
        REQUIRE(chunk.payload.size() % kPayloadAlignment == 0);
        REQUIRE(chunk.type <= chunk_type::kHighest);
        expected = chunk.payload.data() + chunk.payload.size() + kHeaderSize;
    }
    return expected - kHeaderSize;
}

void check_flat(const Buffer& data) {
    const auto flat = parse_flat(data);
    if (!flat) {
        REQUIRE(flat.error().offset <= data.size());
        return;
    }
    REQUIRE(check_resource(*flat, data) == data.data() + data.size());
}

void check_pack(const Buffer& data) {
    const auto pack = parse_pack(data);
    if (!pack) {
        REQUIRE(pack.error().offset <= data.size());
        return;
    }
    REQUIRE(pack->resources.size() >= kMinPackResources);
    const std::byte* end = nullptr;
    for (const Resource& resource : pack->resources) {
        end = check_resource(resource, data);
    }
    REQUIRE(end == data.data() + data.size());
}

// The offsets of every header in a valid container: each chunk's header sits 16 bytes before its payload, and a
// resource's header 16 bytes before its first chunk's header.
std::vector<std::size_t> header_offsets(const Buffer& data, const std::vector<Resource>& resources) {
    std::vector<std::size_t> offsets;
    for (const Resource& resource : resources) {
        const auto first = static_cast<std::size_t>(resource.chunks.front().payload.data() - data.data());
        offsets.push_back(first - 2 * kHeaderSize);
        for (const Chunk& chunk : resource.chunks) {
            offsets.push_back(static_cast<std::size_t>(chunk.payload.data() - data.data()) - kHeaderSize);
        }
    }
    return offsets;
}

Buffer sample_pack() {
    return make_pack({sample_flat(), make_resource(7, {{chunk_type::kModel, 7, 48}, {chunk_type::kBoneOffsets, 7, 16}}),
                      make_resource(9, {{chunk_type::kSceneList, 9, 0}})});
}

} // namespace

TEST_CASE("every truncation of a valid container is rejected") {
    const Buffer flat = sample_flat();
    for (std::size_t size = 0; size < flat.size(); ++size) {
        const Buffer cut(flat.begin(), flat.begin() + static_cast<std::ptrdiff_t>(size));
        INFO("flat cut to " << size);
        CHECK_FALSE(parse_flat(cut).has_value());
        check_flat(cut);
    }
    const Buffer pack = sample_pack();
    for (std::size_t size = 0; size < pack.size(); ++size) {
        const Buffer cut(pack.begin(), pack.begin() + static_cast<std::ptrdiff_t>(size));
        INFO("pack cut to " << size);
        CHECK_FALSE(parse_pack(cut).has_value());
        check_pack(cut);
    }
}

TEST_CASE("every header field of a valid container set to edge values stays safe") {
    const Buffer flat = sample_flat();
    const auto flat_read = parse_flat(flat);
    REQUIRE(flat_read.has_value());
    for (const std::size_t header : header_offsets(flat, {*flat_read})) {
        for (std::size_t field = 0; field < kHeaderSize; field += 4) {
            for (const std::uint32_t value : kEdgeValues) {
                Buffer data = flat;
                poke_u32(data, header + field, value);
                INFO("flat header " << header << " field " << field << " value " << value);
                check_flat(data);
            }
        }
    }

    const Buffer pack = sample_pack();
    const auto pack_read = parse_pack(pack);
    REQUIRE(pack_read.has_value());
    std::vector<std::size_t> headers = header_offsets(pack, pack_read->resources);
    headers.push_back(0);
    for (const std::size_t header : headers) {
        for (std::size_t field = 0; field < kHeaderSize; field += 4) {
            for (const std::uint32_t value : kEdgeValues) {
                Buffer data = pack;
                poke_u32(data, header + field, value);
                INFO("pack header " << header << " field " << field << " value " << value);
                check_pack(data);
            }
        }
    }
}

TEST_CASE("every size field of a valid container changed by a header's length is rejected") {
    const Buffer pack = sample_pack();
    const auto read = parse_pack(pack);
    REQUIRE(read.has_value());
    std::vector<std::size_t> headers = header_offsets(pack, read->resources);
    headers.push_back(0);
    for (const std::size_t header : headers) {
        for (const std::int64_t delta : {-16, 16}) {
            Buffer data = pack;
            const auto size = static_cast<std::int64_t>(peek_u32(data, header + 4)) + delta;
            poke_u32(data, header + 4, static_cast<std::uint32_t>(size));
            INFO("header " << header << " size changed by " << delta);
            CHECK_FALSE(parse_pack(data).has_value());
        }
    }
}

TEST_CASE("every byte of a valid container flipped stays safe") {
    const Buffer pack = sample_pack();
    for (std::size_t at = 0; at < pack.size(); ++at) {
        for (const std::byte mask : {std::byte{0x01}, std::byte{0x80}, std::byte{0xFF}}) {
            Buffer data = pack;
            data[at] ^= mask;
            INFO("byte " << at << " flipped with " << std::to_integer<int>(mask));
            check_pack(data);
            check_flat(data);
        }
    }
}

TEST_CASE("random corruption of a valid container stays safe") {
    // minstd_rand's sequence is fixed by the standard, so every run tries the same inputs.
    std::minstd_rand random(20261017);
    const Buffer pack = sample_pack();
    for (int round = 0; round < 20000; ++round) {
        Buffer data = pack;
        const auto edits = 1 + random() % 4;
        for (std::uint_fast32_t edit = 0; edit < edits; ++edit) {
            const std::size_t at = random() % data.size();
            data[at] = static_cast<std::byte>(random() & 0xFF);
        }
        data.resize(data.size() - random() % 3 * 16);
        INFO("round " << round);
        check_pack(data);
        check_flat(data);
    }
}
