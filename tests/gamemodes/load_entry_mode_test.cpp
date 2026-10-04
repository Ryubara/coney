// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/load_entry_mode.h"

#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "core/game_timer.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "support/fixtures.h"

using coney::nameHash;
using coney::test::Bytes;
using coney::test::TempDir;

namespace {

/// A synthetic disc image with three WAD entries: a flat container, a pack and some text.
std::vector<std::byte> buildDisc() {
    Bytes flat;
    flat.header(2, 32, 0, 0);
    flat.header(0x17, 16, 0, 0).fill(16, 1);
    flat.header(0x0E, 16, 0, 0).fill(16, 2);

    Bytes pack;
    pack.header(2, 0, 0, coney::chunk::kPackageMarker);
    pack.header(1, 16, 0, 0x1234).header(0x2A, 16, 0, 0x1234).fill(16, 3);
    pack.header(2, 32, 0, 0x5678).header(0x47, 16, 0, 0).fill(16, 4).header(0x28, 16, 0, 0).fill(16, 5);

    Bytes text;
    text.text("-- not a container\n");

    Bytes wad;
    Bytes dir;
    dir.header(3, 0, 0, 0);
    for (const auto& [name, data] :
         {std::pair{"test.lev", &flat}, std::pair{"test.pak", &pack}, std::pair{"test.lua", &text}}) {
        dir.u32(static_cast<std::uint32_t>(wad.size()))
            .u32(static_cast<std::uint32_t>(data->size()))
            .u32(nameHash(std::string("./ee_files/") + name));
        wad.append(data->span());
        wad.padTo((wad.size() + 2047) / 2048 * 2048);
    }
    return coney::test::buildIso({{"WARRIORS.DIR;1", dir.data()}, {"WARRIORS.WAD;1", wad.data()}});
}

} // namespace

TEST_CASE("the load mode loads one entry per frame and leaves", "[load_entry_mode]") {
    TempDir dir;
    const auto image = dir.write("disc.iso", buildDisc());
    auto disc = coney::io::Disc::open(image);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    const auto table = coney::chunk::ChunkHandlerTable::withDefaults();

    std::string output;
    coney::LoadEntryMode mode(*wad, table, {"TEST.LEV", "test.pak", "test.lua", "missing.bin"},
                              [&output](std::string_view text) { output += text; });
    coney::GameModeStack stack;
    stack.push(mode);
    coney::GameTimer timer;
    CHECK(stack.runUntilEmpty(timer, {}, std::nullopt) == 4);
    CHECK(mode.failures() == 2);

    CAPTURE(output);
    CHECK(output.find("flat container: 2 chunks, 32 bytes") != std::string::npos);
    CHECK(output.find("type 0x14 Null Pointer") == std::string::npos); // the summary lists the chunks as read
    CHECK(output.find("type 0x0e Camera Animation: 1 chunk, 16 bytes") != std::string::npos);
    CHECK(output.find("grouped container (pack): 2 groups") != std::string::npos);
    CHECK(output.find("left on the stacks: 3 chunks, 0 objects; 0 trailing bytes") != std::string::npos);
    CHECK(output.find("test.lua: entry 2") != std::string::npos);
    CHECK(output.find("not a chunk container") != std::string::npos);
    CHECK(output.find("missing.bin: no WAD entry") != std::string::npos);
}

TEST_CASE("loadWadEntry reports what the handlers left", "[load_entry_mode]") {
    TempDir dir;
    auto disc = coney::io::Disc::open(dir.write("disc.iso", buildDisc()));
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    const auto table = coney::chunk::ChunkHandlerTable::withDefaults();
    auto load = coney::loadWadEntry(*wad, *wad->lookup("test.lev").value(), table);
    REQUIRE(load.has_value());
    CHECK(load->kind == coney::ContainerKind::Flat);
    REQUIRE(load->stacks.chunks().size() == 2);
    CHECK(load->stacks.chunks()[1].type == coney::chunk::kNullPointer);
}
