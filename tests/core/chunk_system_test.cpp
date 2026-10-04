// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chunk_system.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_stacks.h"
#include "core/chunk_types.h"
#include "fileio/stream.h"
#include "support/fixtures.h"

using coney::Error;
using coney::ErrorCode;
using coney::chunk::ChunkData;
using coney::chunk::ChunkHandlers;
using coney::chunk::ChunkHandlerTable;
using coney::chunk::ChunkHeader;
using coney::chunk::ChunkStacks;
using coney::chunk::LoadedObject;
using coney::io::MemoryStream;
using coney::io::Stream;
using coney::test::Bytes;

namespace {

/// Appends a chunk: its header, then `size` bytes of `fill`.
Bytes& chunk(Bytes& bytes, std::uint32_t type, std::uint32_t size, std::uint8_t fill, std::uint32_t id = 0) {
    bytes.header(type, size, 0, id);
    return bytes.fill(size, fill);
}

// An object a handler can push, carrying a value the test checks.
struct TestObject final : LoadedObject {
    explicit TestObject(int v) : value(v) {}
    [[nodiscard]] std::string_view describe() const override { return "test object"; }
    int value;
};

// A second object type, for checking that popObject<T>() refuses the wrong one.
struct OtherObject final : LoadedObject {
    [[nodiscard]] std::string_view describe() const override { return "other object"; }
};

} // namespace

TEST_CASE("every chunk type has its table name", "[chunk_system]") {
    CHECK(coney::chunk::chunkTypeName(0x00) == "Anim Rot Keyframes");
    CHECK(coney::chunk::chunkTypeName(0x17) == "Level Header");
    CHECK(coney::chunk::chunkTypeName(0x53) == "Occluders");
    CHECK(coney::chunk::chunkTypeName(0x54).empty());
    for (std::uint32_t type = 0; type < coney::chunk::kChunkTypeCount; ++type) {
        CHECK_FALSE(coney::chunk::chunkTypeName(type).empty());
    }
}

TEST_CASE("a flat container's chunks are pushed raw in file order", "[chunk_system]") {
    // Three chunks of 16, 0 and 32 bytes; the header's id (0x99) and the first chunk's id (7) are kept for checking.
    Bytes bytes;
    bytes.header(3, 48, 0, 0x99);
    chunk(bytes, 0x04, 16, 0xAA, 7);
    chunk(bytes, 0x05, 0, 0);
    chunk(bytes, 0x03, 32, 0xBB);
    MemoryStream stream(bytes.span());
    ChunkStacks stacks;
    auto report = coney::chunk::loadContainer(stream, ChunkHandlerTable{}, stacks);
    REQUIRE(report.has_value());
    CHECK(report->container.count == 3);
    CHECK(report->container.payloadBytes == 48);
    CHECK(report->container.id == 0x99);
    REQUIRE(report->chunks.size() == 3);
    CHECK(report->chunks[0].header.id == 7);
    CHECK(stream.remaining() == 0);

    REQUIRE(stacks.chunks().size() == 3);
    CHECK(stacks.peekChunkType() == 0x03);
    auto top = stacks.popChunk(0x03);
    REQUIRE(top.has_value());
    CHECK(top->bytes.size() == 32);
    CHECK(top->bytes[31] == std::byte{0xBB});
    CHECK(stacks.popChunk(0x05)->bytes.empty());
    CHECK(stacks.popChunk(0x04)->id == 7);
    CHECK_FALSE(stacks.peekChunkType().has_value());
}

TEST_CASE("pops check the type and leave the stack alone on a mismatch", "[chunk_system]") {
    ChunkStacks stacks;
    auto empty = stacks.popChunk(0x01);
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().code == ErrorCode::Invalid);

    stacks.pushChunk(ChunkData{0x16, 0, {}, nullptr});
    auto wrong = stacks.popChunk(0x17);
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().message.find("Level Header") != std::string::npos);
    CHECK(stacks.chunks().size() == 1);

    CHECK_FALSE(stacks.popAnyObject().has_value());
    stacks.pushObject(std::make_unique<TestObject>(5));
    auto mismatch = stacks.popObject<OtherObject>();
    REQUIRE_FALSE(mismatch.has_value());
    CHECK(mismatch.error().message.find("test object") != std::string::npos);
    REQUIRE(stacks.objects().size() == 1);
    auto object = stacks.popObject<TestObject>();
    REQUIRE(object.has_value());
    CHECK((*object)->value == 5);
}

TEST_CASE("the default table retags camera animations and scripts as null pointers", "[chunk_system]") {
    Bytes bytes;
    bytes.header(3, 0, 0, 0);
    chunk(bytes, 0x0E, 16, 1);
    chunk(bytes, 0x30, 16, 2);
    chunk(bytes, 0x14, 16, 3);
    MemoryStream stream(bytes.span());
    ChunkStacks stacks;
    auto report = coney::chunk::loadContainer(stream, ChunkHandlerTable::withDefaults(), stacks);
    REQUIRE(report.has_value());
    REQUIRE(stacks.chunks().size() == 3);
    for (const ChunkData& data : stacks.chunks()) {
        CHECK(data.type == coney::chunk::kNullPointer);
    }
    CHECK(stacks.chunks()[1].bytes[0] == std::byte{2});
}

TEST_CASE("an on-loaded handler pops the chunks before it and pushes an object", "[chunk_system]") {
    ChunkHandlerTable table;
    table.setHandlers(0x24, ChunkHandlers{[](ChunkStacks& stacks, std::uint32_t type) -> std::expected<void, Error> {
                                              auto list = stacks.popChunk(type);
                                              auto definition = stacks.popChunk(0x23);
                                              auto instance = stacks.popChunk(0x22);
                                              if (!list || !definition || !instance) {
                                                  return coney::fail(ErrorCode::Invalid, "missing chunk");
                                              }
                                              stacks.pushObject(std::make_unique<TestObject>(
                                                  static_cast<int>(definition->bytes.size())));
                                              return {};
                                          },
                                          {}});
    // Instance, definition, then the list whose handler pops both: the order the handler expects.
    Bytes good;
    good.header(3, 0, 0, 0);
    chunk(good, 0x22, 16, 0);
    chunk(good, 0x23, 32, 0);
    chunk(good, 0x24, 16, 0);
    MemoryStream stream(good.span());
    ChunkStacks stacks;
    REQUIRE(coney::chunk::loadContainer(stream, table, stacks).has_value());
    CHECK(stacks.chunks().empty());
    auto object = stacks.popObject<TestObject>();
    REQUIRE(object.has_value());
    CHECK((*object)->value == 32);

    // The same chunks out of order: the handler's pop fails and so does the load, naming the chunk.
    Bytes bad;
    bad.header(3, 0, 0, 0);
    chunk(bad, 0x23, 16, 0);
    chunk(bad, 0x22, 16, 0);
    chunk(bad, 0x24, 16, 0);
    MemoryStream badStream(bad.span());
    ChunkStacks badStacks;
    auto failed = coney::chunk::loadContainer(badStream, table, badStacks);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().message.find("chunk 2") != std::string::npos);
}

TEST_CASE("a stream reader sees only its chunk and the rest is skipped", "[chunk_system]") {
    ChunkHandlerTable table;
    std::uint64_t seenSize = 0;
    table.setHandlers(0x47, ChunkHandlers{{},
                                          [&seenSize](Stream& data, const ChunkHeader& header,
                                                      ChunkStacks& stacks) -> std::expected<void, Error> {
                                              seenSize = data.size();
                                              auto first = data.readU32Le();
                                              if (!first) {
                                                  return std::unexpected(first.error());
                                              }
                                              CHECK(header.type == 0x47);
                                              stacks.pushChunk(ChunkData{0x41, header.id, {}, nullptr});
                                              return {};
                                          }});
    // A 64-byte chunk for the reader, then a raw chunk whose bytes show the reader did not eat into it.
    Bytes bytes;
    bytes.header(2, 0, 0, 0);
    chunk(bytes, 0x47, 64, 0x11, 5);
    chunk(bytes, 0x28, 16, 0x22);
    MemoryStream stream(bytes.span());
    ChunkStacks stacks;
    auto report = coney::chunk::loadContainer(stream, table, stacks);
    REQUIRE(report.has_value());
    CHECK(seenSize == 64);
    CHECK(report->chunks[0].readByHandler);
    CHECK_FALSE(report->chunks[1].readByHandler);
    REQUIRE(stacks.chunks().size() == 2);
    CHECK(stacks.chunks()[0].type == 0x41);
    CHECK(stacks.chunks()[1].bytes[0] == std::byte{0x22});

    // A reader that tries to read past its chunk fails instead of reading the next chunk.
    ChunkHandlerTable greedy;
    greedy.setHandlers(0x47,
                       ChunkHandlers{{}, [](Stream& data, const ChunkHeader&, ChunkStacks&) { return data.skip(65); }});
    MemoryStream again(bytes.span());
    ChunkStacks moreStacks;
    auto failed = coney::chunk::loadContainer(again, greedy, moreStacks);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == ErrorCode::Truncated);
}

TEST_CASE("malformed flat containers fail without reading past the end", "[chunk_system]") {
    ChunkStacks stacks;
    const ChunkHandlerTable table;

    SECTION("a header cut short") {
        Bytes bytes;
        bytes.u32(1).u32(0); // 8 of the header's 16 bytes
        MemoryStream stream(bytes.span());
        CHECK(coney::chunk::loadContainer(stream, table, stacks).error().code == ErrorCode::Truncated);
    }
    SECTION("a count far larger than the data") {
        Bytes bytes;
        bytes.header(0x10000000, 0, 0, 0);
        chunk(bytes, 0x04, 16, 0);
        MemoryStream stream(bytes.span());
        CHECK(coney::chunk::loadContainer(stream, table, stacks).error().code == ErrorCode::Truncated);
        CHECK(stacks.chunks().empty());
    }
    SECTION("a chunk size past the end") {
        Bytes bytes;
        bytes.header(1, 0, 0, 0);
        bytes.header(0x04, 0xFFFFFFF0U, 0, 0);
        bytes.fill(16, 0);
        MemoryStream stream(bytes.span());
        auto result = coney::chunk::loadContainer(stream, table, stacks);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::Truncated);
        CHECK(result.error().message.find("Collision Triangles") != std::string::npos);
    }
    SECTION("a type outside the table") {
        Bytes bytes;
        bytes.header(1, 0, 0, 0);
        chunk(bytes, 0x54, 16, 0);
        MemoryStream stream(bytes.span());
        CHECK(coney::chunk::loadContainer(stream, table, stacks).error().code == ErrorCode::Invalid);
    }
    SECTION("fewer chunks than the count") {
        Bytes bytes;
        bytes.header(3, 0, 0, 0);
        chunk(bytes, 0x04, 0, 0);
        chunk(bytes, 0x04, 0, 0);
        bytes.u32(0x04);
        MemoryStream stream(bytes.span());
        CHECK(coney::chunk::loadContainer(stream, table, stacks).error().code == ErrorCode::Truncated);
    }
}

TEST_CASE("a grouped container loads every group onto one pair of stacks", "[chunk_system]") {
    // A pack of up to three groups: two chunks in resource 0xAAAA, one in 0xBBBB, then a zero group.
    Bytes bytes;
    bytes.header(3, 0, 0, coney::chunk::kPackageMarker);
    bytes.header(2, 32, 0, 0xAAAA);
    chunk(bytes, 0x00, 16, 1);
    chunk(bytes, 0x02, 16, 2);
    bytes.header(1, 0, 0, 0xBBBB);
    chunk(bytes, 0x2A, 0, 0);
    bytes.header(0, 0, 0, 0); // resource id 0 ends the container before the third group
    MemoryStream stream(bytes.span());
    ChunkStacks stacks;
    auto report = coney::chunk::loadGroupedContainer(stream, ChunkHandlerTable{}, stacks);
    REQUIRE(report.has_value());
    REQUIRE(report->groups.size() == 2);
    CHECK(report->groups[0].resourceId == 0xAAAA);
    CHECK(report->groups[1].chunkCount == 1);
    CHECK(report->endedByZeroGroup);
    CHECK(report->chunks.size() == 3);
    CHECK(stacks.chunks().size() == 3);
    CHECK(stream.remaining() == 0);
}

TEST_CASE("a grouped container may end after its count without a zero group", "[chunk_system]") {
    Bytes bytes;
    bytes.header(1, 0, 0, 0);
    bytes.header(1, 16, 0, 0x1);
    chunk(bytes, 0x47, 16, 0);
    MemoryStream stream(bytes.span());
    ChunkStacks stacks;
    auto report = coney::chunk::loadGroupedContainer(stream, ChunkHandlerTable{}, stacks);
    REQUIRE(report.has_value());
    CHECK_FALSE(report->endedByZeroGroup);
    CHECK(report->groups.size() == 1);
}

TEST_CASE("malformed grouped containers fail with the group named", "[chunk_system]") {
    ChunkStacks stacks;
    // A good first group, then a second whose chunk count cannot fit in what is left.
    Bytes bytes;
    bytes.header(2, 0, 0, 0);
    bytes.header(1, 16, 0, 0x1);
    chunk(bytes, 0x47, 16, 0);
    bytes.header(0x7FFFFFFF, 16, 0, 0x2);
    MemoryStream stream(bytes.span());
    auto result = coney::chunk::loadGroupedContainer(stream, ChunkHandlerTable{}, stacks);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::Truncated);
    CHECK(result.error().message.find("group 1") != std::string::npos);
}

TEST_CASE("takeChunks removes every chunk of a type and keeps the others in order", "[chunk_system]") {
    ChunkStacks stacks;
    for (const std::uint32_t type : {0x0BU, 0x17U, 0x0BU, 0x4CU}) {
        ChunkData chunk;
        chunk.type = type;
        chunk.id = static_cast<std::uint32_t>(stacks.chunks().size());
        stacks.pushChunk(std::move(chunk));
    }
    auto taken = stacks.takeChunks(0x0B);
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].id == 0);
    CHECK(taken[1].id == 2);
    REQUIRE(stacks.chunks().size() == 2);
    CHECK(stacks.chunks()[0].type == 0x17);
    CHECK(stacks.chunks()[1].type == 0x4C);
    CHECK(stacks.takeChunks(0x0B).empty());
}
