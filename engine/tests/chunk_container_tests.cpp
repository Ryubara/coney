#include "chunk_builders.hpp"
#include "formats/chunk_container.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <vector>

using namespace coney::formats;
using namespace coney::test;

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
        CHECK(parse_flat({}).error().kind == ErrorKind::TruncatedHeader);
        Buffer data = sample_flat();
        data.resize(15);
        CHECK(parse_flat(data).error().kind == ErrorKind::TruncatedHeader);
    }
    SECTION("chunk header cut short") {
        Buffer data = sample_flat();
        data.resize(16 + 7);
        CHECK(parse_flat(data).error().kind == ErrorKind::CountExceedsBytes);
        data = make_resource(1, {{chunk_type::kModel, 1, 16}, {chunk_type::kModel, 1, 16}});
        data.resize(16 + 32 + 7);
        CHECK(parse_flat(data).error().kind == ErrorKind::TruncatedHeader);
    }
    SECTION("payload past the end") {
        Buffer data = sample_flat();
        data.resize(data.size() - 1 - 16);
        CHECK(parse_flat(data).error().kind == ErrorKind::PayloadPastEnd);
        Buffer huge = make_resource(1, {{chunk_type::kModel, 1, 16}});
        poke_u32(huge, 16 + 4, 0xFFFFFFF0);
        CHECK(parse_flat(huge).error().kind == ErrorKind::PayloadPastEnd);
    }
    SECTION("bytes left over") {
        Buffer data = sample_flat();
        data.push_back(std::byte{0});
        CHECK(parse_flat(data).error().kind == ErrorKind::TrailingBytes);
    }
    SECTION("size breaks the alignment rule") {
        Buffer data = make_resource(1, {{chunk_type::kModel, 1, 24}});
        CHECK(parse_flat(data).error().kind == ErrorKind::MisalignedSize);
    }
    SECTION("reserved words and sums") {
        Buffer data = sample_flat();
        poke_u32(data, 8, 1);
        CHECK(parse_flat(data).error().kind == ErrorKind::NonZeroReserved);
        data = sample_flat();
        poke_u32(data, 16 + 8, 1);
        CHECK(parse_flat(data).error().kind == ErrorKind::NonZeroReserved);
        data = sample_flat();
        poke_u32(data, 4, 1);
        CHECK(parse_flat(data).error().kind == ErrorKind::ChunkSumMismatch);
        data = sample_flat();
        poke_u32(data, 16, 0x54);
        CHECK(parse_flat(data).error().kind == ErrorKind::UnknownChunkType);
        data = sample_flat();
        poke_u32(data, 0, 0xFFFFFFFF);
        CHECK(parse_flat(data).error().kind == ErrorKind::CountExceedsBytes);
    }
}

TEST_CASE("malformed packs are rejected") {
    const Buffer good = make_pack({sample_flat(), sample_flat()});
    REQUIRE(parse_pack(good).has_value());

    SECTION("truncated header") {
        Buffer data = good;
        data.resize(10);
        CHECK(parse_pack(data).error().kind == ErrorKind::TruncatedHeader);
    }
    SECTION("wrong pack hash") {
        Buffer data = good;
        poke_u32(data, 12, 0xDE686796);
        CHECK(parse_pack(data).error().kind == ErrorKind::BadPackHash);
    }
    SECTION("bytes left over") {
        Buffer data = good;
        data.insert(data.end(), 16, std::byte{0});
        poke_u32(data, 4, static_cast<std::uint32_t>(data.size() - 16));
        CHECK(parse_pack(data).error().kind == ErrorKind::TrailingBytes);
    }
    SECTION("size field disagrees") {
        Buffer data = good;
        data.push_back(std::byte{0});
        CHECK(parse_pack(data).error().kind == ErrorKind::PackSizeMismatch);
    }
    SECTION("resource count too high") {
        Buffer data = good;
        poke_u32(data, 0, 3);
        CHECK(parse_pack(data).error().kind == ErrorKind::TruncatedHeader);
        poke_u32(data, 0, 0xFFFFFFFF);
        CHECK(parse_pack(data).error().kind == ErrorKind::CountExceedsBytes);
    }
    SECTION("a bad resource inside") {
        Buffer data = make_pack({sample_flat(), make_resource(1, {{chunk_type::kModel, 1, 24}})});
        CHECK(parse_pack(data).error().kind == ErrorKind::MisalignedSize);
    }
}

TEST_CASE("every error has a description") {
    CHECK(std::string(describe(ErrorKind::BadPackHash)) == "pack header does not end with the hash of \"package\"");
}

TEST_CASE("counts below the format's minimum are rejected") {
    SECTION("a resource with no chunks") {
        const Buffer empty = make_resource(1, {});
        CHECK(parse_flat(empty).error() == ParseError{ErrorKind::CountTooLow, 0});
        const Buffer pack = make_pack({sample_flat(), empty});
        CHECK(parse_pack(pack).error() == ParseError{ErrorKind::CountTooLow, 16 + sample_flat().size()});
    }
    SECTION("a resource with one chunk is the smallest") {
        CHECK(parse_flat(make_resource(1, {{chunk_type::kModel, 1, 0}})).has_value());
    }
    SECTION("packs of no or one resource") {
        CHECK(parse_pack(make_pack({})).error() == ParseError{ErrorKind::CountTooLow, 0});
        CHECK(parse_pack(make_pack({sample_flat()})).error() == ParseError{ErrorKind::CountTooLow, 0});
    }
}

TEST_CASE("errors give the offset of the fault") {
    SECTION("a chunk header inside a flat container") {
        Buffer data = sample_flat();
        poke_u32(data, 16 + 16 + 32 + 8, 1);
        CHECK(parse_flat(data).error() == ParseError{ErrorKind::NonZeroReserved, 16 + 16 + 32});
    }
    SECTION("bytes left over") {
        Buffer data = sample_flat();
        const std::size_t end = data.size();
        data.insert(data.end(), 16, std::byte{0});
        CHECK(parse_flat(data).error() == ParseError{ErrorKind::TrailingBytes, end});
    }
    SECTION("a chunk of the second resource in a pack") {
        const Buffer first = sample_flat();
        Buffer data = make_pack({first, make_resource(7, {{chunk_type::kModel, 7, 48}})});
        const std::size_t second = 16 + first.size();
        poke_u32(data, second + 16 + 4, 40);
        CHECK(parse_pack(data).error() == ParseError{ErrorKind::MisalignedSize, second + 16});
    }
    SECTION("the description names the offset") {
        CHECK(describe(ParseError{ErrorKind::TrailingBytes, 0x40}) == "bytes left over at offset 0x40");
    }
}

TEST_CASE("edge sizes parse") {
    SECTION("the highest type id, empty payloads and a payload that ends at the last byte") {
        const Buffer data = make_resource(1, {{chunk_type::kHighest, 1, 0}, {chunk_type::kAnimationData, 2, 0},
                                              {chunk_type::kModel, 3, 4096}});
        const auto resource = parse_flat(data);
        REQUIRE(resource.has_value());
        CHECK(resource->chunks[0].type == chunk_type::kHighest);
        CHECK(resource->chunks[0].payload.empty());
        CHECK(resource->chunks[2].payload.data() + 4096 == data.data() + data.size());
    }
    SECTION("1,190 chunks, the most in one resource on the disc") {
        std::vector<ChunkSpec> chunks;
        for (std::uint32_t i = 0; i < 1190; ++i) {
            chunks.push_back({i % 2 == 0 ? chunk_type::kAnimationData : chunk_type::kClipDescriptor, i, 16 * (i % 3)});
        }
        const auto resource = parse_flat(make_resource(5, chunks));
        REQUIRE(resource.has_value());
        REQUIRE(resource->chunks.size() == 1190);
        CHECK(resource->chunks[1189].hash == 1189);
        CHECK(resource->chunks[1189].payload.size() == 16);
    }
    SECTION("351 resources, the most in one pack on the disc") {
        std::vector<Buffer> resources;
        for (std::uint32_t i = 0; i < 351; ++i) {
            resources.push_back(make_resource(i, {{chunk_type::kTextureDictionary, i, 16}}));
        }
        const auto pack = parse_pack(make_pack(resources));
        REQUIRE(pack.has_value());
        REQUIRE(pack->resources.size() == 351);
        CHECK(pack->resources[350].hash == 350);
    }
    SECTION("input that does not start on an aligned address") {
        const Buffer flat = sample_flat();
        Buffer shifted(1);
        shifted.insert(shifted.end(), flat.begin(), flat.end());
        const auto resource = parse_flat(std::span(shifted).subspan(1));
        REQUIRE(resource.has_value());
        CHECK(resource->hash == 0x11223344);
    }
}

TEST_CASE("a pack's resources and chunks view the right bytes") {
    const Buffer first = make_resource(0xA, {{chunk_type::kAnimationData, 0xA0, 32, 0x10},
                                             {chunk_type::kClipDescriptor, 0xA1, 80, 0x11}});
    const Buffer second = make_resource(0xB, {{chunk_type::kBoneOffsets, 0xB0, 16, 0x20},
                                              {chunk_type::kModel, 0xB1, 48, 0x21},
                                              {chunk_type::kParticlePage, 0xB2, 0, 0}});
    const Buffer data = make_pack({first, second, first});
    const auto pack = parse_pack(data);
    REQUIRE(pack.has_value());
    REQUIRE(pack->resources.size() == 3);

    // The same resource may appear twice in a pack; the reader keeps both.
    CHECK(pack->resources[0].hash == 0xA);
    CHECK(pack->resources[2].hash == 0xA);

    const std::byte* second_start = data.data() + 16 + first.size();
    const Resource& middle = pack->resources[1];
    CHECK(middle.hash == 0xB);
    REQUIRE(middle.chunks.size() == 3);
    CHECK(middle.chunks[0].payload.data() == second_start + 32);
    CHECK(middle.chunks[1].payload.data() == second_start + 32 + 16 + 16);
    CHECK(middle.chunks[1].hash == 0xB1);
    CHECK(middle.chunks[1].payload.front() == std::byte{0x21});
    CHECK(middle.chunks[1].payload.back() == std::byte{0x21});
    CHECK(middle.chunks[2].payload.data() == second_start + second.size());
    CHECK(pack->resources[2].chunks[1].payload.data() + 80 == data.data() + data.size());
}

TEST_CASE("every error kind has its own description") {
    const std::vector<ErrorKind> kinds{
        ErrorKind::TruncatedHeader, ErrorKind::PayloadPastEnd,   ErrorKind::BadPackHash,
        ErrorKind::TrailingBytes,   ErrorKind::MisalignedSize,   ErrorKind::NonZeroReserved,
        ErrorKind::UnknownChunkType, ErrorKind::ChunkSumMismatch, ErrorKind::PackSizeMismatch,
        ErrorKind::CountExceedsBytes, ErrorKind::CountTooLow};
    std::vector<std::string> seen;
    for (const ErrorKind kind : kinds) {
        const std::string text = describe(kind);
        CHECK(text != "unknown error");
        CHECK(std::ranges::find(seen, text) == seen.end());
        seen.push_back(text);
    }
}
