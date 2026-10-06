// SPDX-License-Identifier: GPL-3.0-or-later
// The movies' captions (docs/research/movies.md#captions): the Subtitles chunk, the caption state, the timing, and the
// level file a movie's captions come from. Synthetic chunks; nothing from the disc.
#include "movies/captions.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "movies/disc_captions.h"

namespace {

using coney::movies::CaptionKind;
using coney::movies::CaptionRecord;
using coney::movies::Captions;
using coney::movies::CaptionTimeline;

// A Subtitles chunk of `records`: the u16 length, then each record's u32 kind and NUL-terminated text.
std::vector<std::byte> subtitlesChunk(const std::vector<CaptionRecord>& records) {
    std::vector<std::byte> chunk(2);
    for (const CaptionRecord& record : records) {
        for (int i = 0; i < 4; ++i) {
            chunk.push_back(static_cast<std::byte>((record.kind >> (8 * i)) & 0xffU));
        }
        for (const char c : record.text) {
            chunk.push_back(static_cast<std::byte>(c));
        }
        chunk.push_back(std::byte{0});
    }
    // The length counts the whole chunk; a record is read while it starts before length - 1.
    chunk.push_back(std::byte{0});
    chunk[0] = static_cast<std::byte>(chunk.size() & 0xffU);
    chunk[1] = static_cast<std::byte>(chunk.size() >> 8);
    return chunk;
}

// Two languages, each with a movie's captions and a scene's.
std::vector<CaptionRecord> twoLanguages() {
    return {{0, "ENGLISH"}, {1, "l99_in_sub"}, {3, "first"},      {3, "second"}, {1, "l99_c1"},
            {2, "loud"},    {0, "GERMAN"},     {1, "l99_in_sub"}, {3, "erste"},  {9, "zweite"}};
}

} // namespace

TEST_CASE("captions: the Subtitles chunk reads back its records", "[captions]") {
    const auto records = coney::movies::parseSubtitles(subtitlesChunk(twoLanguages()));
    REQUIRE(records.has_value());
    REQUIRE(records->size() == 10);
    CHECK((*records)[1].text == "l99_in_sub");
    CHECK((*records)[9].kind == 9);
    CHECK(coney::movies::captionKind(9) == CaptionKind::Ordinary);
    CHECK(coney::movies::captionKind(2) == CaptionKind::Emphasised);

    std::vector<std::byte> cut = subtitlesChunk(twoLanguages());
    cut.resize(cut.size() - 8);
    CHECK_FALSE(coney::movies::parseSubtitles(cut).has_value());
    CHECK_FALSE(coney::movies::parseSubtitles(std::vector<std::byte>(1)).has_value());
}

TEST_CASE("captions: the language's section, the scene, the next record and the subtitle option", "[captions]") {
    Captions captions(twoLanguages(), "GERMAN");
    CHECK(captions.visible(true) == nullptr);
    CHECK_FALSE(captions.selectScene("l99_c1")); // not in the German section
    CHECK_FALSE(captions.active());
    REQUIRE(captions.selectScene("l99_in_sub"));
    CHECK(captions.visible(true) == nullptr); // nothing shown before the first show
    captions.command(0);
    REQUIRE(captions.visible(true) != nullptr);
    CHECK(captions.visible(true)->text == "erste");
    CHECK(captions.visible(false) == nullptr); // an ordinary caption needs the option
    captions.command(4);
    CHECK(captions.visible(true) == nullptr);
    captions.command(6);
    CHECK(captions.flagged());
    REQUIRE(captions.visible(true) != nullptr);
    CHECK(captions.visible(true)->text == "zweite"); // kind 9 reads as 3
    captions.command(0);                             // past the end: no change
    CHECK(captions.visible(true)->text == "zweite");

    Captions english(twoLanguages(), "ENGLISH");
    REQUIRE(english.selectScene("l99_c1"));
    english.command(0);
    REQUIRE(english.visible(false) != nullptr); // an emphasised caption shows without the option
    CHECK(english.visible(false)->text == "loud");

    Captions none(twoLanguages(), "FRENCH");
    CHECK_FALSE(none.selectScene("l99_in_sub"));
}

TEST_CASE("captions: the styles of the two kinds", "[captions]") {
    const coney::movies::CaptionStyle ordinary = coney::movies::captionStyle(CaptionKind::Ordinary);
    CHECK(ordinary.y == 0.75F);
    CHECK(ordinary.wrapWidth == 0.7F);
    CHECK(ordinary.colour == coney::graphics::Rgba{178, 178, 178, 255});
    const coney::movies::CaptionStyle loud = coney::movies::captionStyle(CaptionKind::Emphasised);
    CHECK(loud.y == 0.5F);
    CHECK(loud.scale == 1.2F);
    CHECK(loud.colour == coney::graphics::Rgba{134, 26, 26, 255});
}

TEST_CASE("captions: the timeline moves on in steps of at least 0.166 s", "[captions]") {
    CaptionTimeline timeline({{30, 4}, {3, 0}, {10, 0}});
    CHECK(timeline.size() == 3);
    CHECK(timeline.advance(1.0 / 30.0).empty());         // 0.033 s since the start: too soon
    CHECK(timeline.advance(0.2) == std::vector<int>{0}); // frame 6: the event at frame 3
    CHECK(timeline.advance(0.3).empty());                // 0.1 s later: too soon, though frame 10 is due
    CHECK(timeline.advance(0.4) == std::vector<int>{0}); // frame 12
    CHECK(timeline.advance(2.0) == std::vector<int>{4}); // frame 60
    CHECK(timeline.advance(3.0).empty());
}

TEST_CASE("captions: the level file of a movie's captions", "[captions]") {
    CHECK(coney::movies::captionLevelFile("L99_IN") == std::optional<std::string>("level99.lev"));
    CHECK(coney::movies::captionLevelFile("L1_IN") == std::optional<std::string>("level1.lev"));
    CHECK(coney::movies::captionLevelFile("L31_OUT") == std::optional<std::string>("level31.lev"));
    CHECK_FALSE(coney::movies::captionLevelFile("LOGO").has_value());
    CHECK_FALSE(coney::movies::captionLevelFile("TRAILER").has_value());
    CHECK(coney::movies::captionLanguageName(coney::Language::German) == "GERMAN");
}
