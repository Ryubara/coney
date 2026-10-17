#include "formats/chunk_container.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

using namespace coney::formats;

namespace {

using Buffer = std::vector<std::byte>;

void put_u32(Buffer& out, std::uint32_t value) {
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

Buffer make_resource(std::uint32_t hash, const std::vector<ChunkSpec>& chunks) {
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

Buffer make_pack(const std::vector<Buffer>& resources) {
    Buffer body;
    for (const auto& r : resources) {
        body.insert(body.end(), r.begin(), r.end());
    }
    Buffer out;
    put_u32(out, static_cast<std::uint32_t>(resources.size()));
    put_u32(out, static_cast<std::uint32_t>(body.size()));
    put_u32(out, 0);
    put_u32(out, kPackEndMarker);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void poke_u32(Buffer& b, std::size_t offset, std::uint32_t value) {
    Buffer tmp;
    put_u32(tmp, value);
    std::memcpy(b.data() + offset, tmp.data(), 4);
}

Buffer sample_flat() {
    return make_resource(0x11223344, {{chunk_type::kAnimationData, 0x11223344, 32},
                                      {chunk_type::kClipDescriptor, 0x55, 80, 0x02},
                                      {chunk_type::kTextureDictionary, 0x66, 0, 0}});
}

} // namespace

TEST_CASE("a flat container with several chunk types parses") {
    const Buffer data = sample_flat();
    const auto resource = parse_flat(data);
    REQUIRE(resource.has_value());
    CHECK(resource->hash == 0x11223344);
    REQUIRE(resource->chunks.size() == 3);
    CHECK(resource->chunks[0].type == chunk_type::kAnimationData);
    CHECK(resource->chunks[0].payload.size() == 32);
    CHECK(resource->chunks[1].type == chunk_type::kClipDescriptor);
    CHECK(resource->chunks[1].hash == 0x55);
    CHECK(resource->chunks[1].payload.size() == 80);
    CHECK(resource->chunks[1].payload[79] == std::byte{0x02});
    CHECK(resource->chunks[1].payload.data() == data.data() + 16 + 16 + 32 + 16);
    CHECK(resource->chunks[2].type == chunk_type::kTextureDictionary);
    CHECK(resource->chunks[2].payload.empty());
}

TEST_CASE("a pack of several resources parses") {
    const Buffer data = make_pack({sample_flat(), make_resource(7, {{chunk_type::kModel, 7, 48}}),
                                   make_resource(9, {{chunk_type::kSceneList, 9, 16}})});
    const auto pack = parse_pack(data);
    REQUIRE(pack.has_value());
    REQUIRE(pack->resources.size() == 3);
    CHECK(pack->resources[0].chunks.size() == 3);
    CHECK(pack->resources[1].hash == 7);
    CHECK(pack->resources[1].chunks[0].type == chunk_type::kModel);
    CHECK(pack->resources[2].chunks[0].payload.size() == 16);
}

TEST_CASE("read_resource reports the bytes it used and leaves the rest") {
    Buffer data = sample_flat();
    const std::size_t used = data.size();
    data.insert(data.end(), 5, std::byte{0});
    const auto read = read_resource(data);
    REQUIRE(read.has_value());
    CHECK(read->consumed == used);
}

TEST_CASE("malformed flat containers are rejected") {
    SECTION("empty and truncated header") {
        CHECK(parse_flat({}).error() == ParseError::TruncatedHeader);
        Buffer data = sample_flat();
        data.resize(15);
        CHECK(parse_flat(data).error() == ParseError::TruncatedHeader);
    }
    SECTION("chunk header cut short") {
        Buffer data = sample_flat();
        data.resize(16 + 7);
        CHECK(parse_flat(data).error() == ParseError::CountExceedsBytes);
        data = make_resource(1, {{chunk_type::kModel, 1, 16}, {chunk_type::kModel, 1, 16}});
        data.resize(16 + 32 + 7);
        CHECK(parse_flat(data).error() == ParseError::TruncatedHeader);
    }
    SECTION("payload past the end") {
        Buffer data = sample_flat();
        data.resize(data.size() - 1 - 16);
        CHECK(parse_flat(data).error() == ParseError::PayloadPastEnd);
        Buffer huge = make_resource(1, {{chunk_type::kModel, 1, 16}});
        poke_u32(huge, 16 + 4, 0xFFFFFFF0);
        CHECK(parse_flat(huge).error() == ParseError::PayloadPastEnd);
    }
    SECTION("bytes left over") {
        Buffer data = sample_flat();
        data.push_back(std::byte{0});
        CHECK(parse_flat(data).error() == ParseError::TrailingBytes);
    }
    SECTION("size breaks the alignment rule") {
        Buffer data = make_resource(1, {{chunk_type::kModel, 1, 24}});
        CHECK(parse_flat(data).error() == ParseError::MisalignedSize);
    }
    SECTION("reserved words and sums") {
        Buffer data = sample_flat();
        poke_u32(data, 8, 1);
        CHECK(parse_flat(data).error() == ParseError::NonZeroReserved);
        data = sample_flat();
        poke_u32(data, 16 + 8, 1);
        CHECK(parse_flat(data).error() == ParseError::NonZeroReserved);
        data = sample_flat();
        poke_u32(data, 4, 1);
        CHECK(parse_flat(data).error() == ParseError::ChunkSumMismatch);
        data = sample_flat();
        poke_u32(data, 16, 0x54);
        CHECK(parse_flat(data).error() == ParseError::UnknownChunkType);
        data = sample_flat();
        poke_u32(data, 0, 0xFFFFFFFF);
        CHECK(parse_flat(data).error() == ParseError::CountExceedsBytes);
    }
}

TEST_CASE("malformed packs are rejected") {
    const Buffer good = make_pack({sample_flat(), sample_flat()});
    REQUIRE(parse_pack(good).has_value());

    SECTION("truncated header") {
        Buffer data = good;
        data.resize(10);
        CHECK(parse_pack(data).error() == ParseError::TruncatedHeader);
    }
    SECTION("wrong end marker") {
        Buffer data = good;
        poke_u32(data, 12, 0xDE686796);
        CHECK(parse_pack(data).error() == ParseError::BadEndMarker);
    }
    SECTION("bytes left over") {
        Buffer data = good;
        data.insert(data.end(), 16, std::byte{0});
        poke_u32(data, 4, static_cast<std::uint32_t>(data.size() - 16));
        CHECK(parse_pack(data).error() == ParseError::TrailingBytes);
    }
    SECTION("size field disagrees") {
        Buffer data = good;
        data.push_back(std::byte{0});
        CHECK(parse_pack(data).error() == ParseError::PackSizeMismatch);
    }
    SECTION("resource count too high") {
        Buffer data = good;
        poke_u32(data, 0, 3);
        CHECK(parse_pack(data).error() == ParseError::TruncatedHeader);
        poke_u32(data, 0, 0xFFFFFFFF);
        CHECK(parse_pack(data).error() == ParseError::CountExceedsBytes);
    }
    SECTION("a bad resource inside") {
        Buffer data = make_pack({make_resource(1, {{chunk_type::kModel, 1, 24}})});
        CHECK(parse_pack(data).error() == ParseError::MisalignedSize);
    }
}

TEST_CASE("every error has a description") {
    CHECK(std::string(describe(ParseError::BadEndMarker)) == "wrong pack end marker");
}
