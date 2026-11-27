// SPDX-License-Identifier: GPL-3.0-or-later
// A human's character as a snapshot names it, and the characters a client keeps for the humans it draws
// (docs/guides/snapshots.md#character, docs/guides/client-world.md#characters): the id's mapping to a Character List
// index, and the roster's choice of character for each id, on synthetic characters.
#include "net/character_id.h"
#include "net/character_roster.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"

using coney::net::characterIdOf;
using coney::net::characterListIndexOf;
using coney::net::CharacterRoster;
using coney::net::kMostCharacterId;
using coney::net::kNoCharacter;

namespace {

// A character a test draws with: the Character List index it was loaded for (kFallbackIndex for the fallback).
struct Drawn {
    std::size_t index = 0;
};
constexpr std::size_t kFallbackIndex = 99999;

// A roster over Drawn characters that records the indexes it was asked to load and fails to load those in `broken`.
struct Fixture {
    std::vector<std::size_t> asked;
    std::vector<std::size_t> broken;
    std::vector<std::string> log;
    CharacterRoster<Drawn> roster{std::make_unique<Drawn>(Drawn{.index = kFallbackIndex}),
                                  [this](std::size_t index) -> std::expected<std::unique_ptr<Drawn>, coney::Error> {
                                      asked.push_back(index);
                                      for (const std::size_t bad : broken) {
                                          if (bad == index) {
                                              return coney::fail(coney::ErrorCode::NotFound, "no such resource");
                                          }
                                      }
                                      return std::make_unique<Drawn>(Drawn{.index = index});
                                  },
                                  [this](std::string_view line) { log.emplace_back(line); }};
};

} // namespace

// Expected: unverified, #1612 (Coney's own; the original keeps the Character List index at human +0x37c,
// characters/index.md)
TEST_CASE("character ids: a Character List index plus one, 0 for none, and back", "[net][character]") {
    CHECK(kNoCharacter == 0);
    CHECK(characterIdOf(0) == 1);
    CHECK(characterIdOf(542) == 543); // the disc's last record
    CHECK(characterIdOf(static_cast<std::size_t>(kMostCharacterId) - 1) == kMostCharacterId);
    // Past the field's range there is no id to send.
    CHECK(characterIdOf(static_cast<std::size_t>(kMostCharacterId)) == kNoCharacter);
    CHECK(characterIdOf(1000000) == kNoCharacter);

    CHECK_FALSE(characterListIndexOf(kNoCharacter).has_value());
    CHECK(characterListIndexOf(1) == std::optional<std::size_t>{0});
    CHECK(characterListIndexOf(543) == std::optional<std::size_t>{542});
    CHECK(characterListIndexOf(kMostCharacterId) == std::optional<std::size_t>{1022});
    CHECK_FALSE(characterListIndexOf(kMostCharacterId + 1).has_value());
    CHECK_FALSE(characterListIndexOf(-5).has_value());
    for (std::size_t index = 0; index < static_cast<std::size_t>(kMostCharacterId); ++index) {
        REQUIRE(characterListIndexOf(characterIdOf(index)) == std::optional<std::size_t>{index});
    }
}

// Expected: unverified, #1612 (Coney's own; the client draws each human with the character his id names)
TEST_CASE("character roster: a human with no character gets the fallback, one with an id gets his own",
          "[net][character]") {
    Fixture fixture;
    CHECK(&fixture.roster.of(kNoCharacter) == &fixture.roster.fallback());
    CHECK(fixture.asked.empty());

    const Drawn& cleon = fixture.roster.of(characterIdOf(12));
    const Drawn& ash = fixture.roster.of(characterIdOf(300));
    CHECK(cleon.index == 12);
    CHECK(ash.index == 300);
    CHECK(&cleon != &ash);
    CHECK(&cleon != &fixture.roster.fallback());
    CHECK(fixture.roster.loaded() == 2);
    CHECK(fixture.log.empty());
    // An id outside the field's range cannot come off the wire; it is the fallback without a load.
    CHECK(&fixture.roster.of(kMostCharacterId + 1) == &fixture.roster.fallback());
    CHECK(&fixture.roster.of(-1) == &fixture.roster.fallback());
    CHECK(fixture.asked == std::vector<std::size_t>{12, 300});
}

// Expected: unverified, #1612 (Coney's own; a character is loaded once, the first time a human of it is drawn)
TEST_CASE("character roster: a character is loaded the first time a human of it is drawn and kept",
          "[net][character]") {
    Fixture fixture;
    const Drawn* first = &fixture.roster.of(characterIdOf(7));
    for (int frame = 0; frame < 5; ++frame) {
        CHECK(&fixture.roster.of(characterIdOf(7)) == first);
    }
    CHECK(fixture.asked == std::vector<std::size_t>{7});
    CHECK(fixture.roster.loaded() == 1);
}

// Expected: unverified, #1612 (Coney's own choice: a character that does not load is drawn as the fallback)
TEST_CASE("character roster: a character that does not load is the fallback, said once and not tried again",
          "[net][character]") {
    Fixture fixture;
    fixture.broken = {40};
    for (int frame = 0; frame < 4; ++frame) {
        CHECK(&fixture.roster.of(characterIdOf(40)) == &fixture.roster.fallback());
    }
    CHECK(fixture.asked == std::vector<std::size_t>{40});
    CHECK(fixture.log.size() == 1);
    CHECK(fixture.roster.loaded() == 0);
    // Others still load.
    CHECK(fixture.roster.of(characterIdOf(41)).index == 41);
    CHECK(fixture.roster.loaded() == 1);
}
