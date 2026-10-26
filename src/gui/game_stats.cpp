// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/game_stats.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

#include "graphics/overlay_camera.h"
#include "gui/markup.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// The value colours: complete, incomplete, the group-1 items' incomplete.
constexpr std::string_view kRed = "861A1AFF";
constexpr std::string_view kGold = "806400FF";
constexpr std::string_view kPurple = "6A4183FF";
// The summary's title colour (grey 178) and its labels' (80, 80, 80).
constexpr std::string_view kTitleGrey = "B2B2B2FF";
constexpr std::string_view kLabelGrey = "505050FF";
// A header's circles when the level's number is 0.
constexpr graphics::Rgba kPurpleRgba{106, 65, 131, 255};
// The specials' pictures (sprite words in record 24).
constexpr std::uint32_t kHubPicture = 0x180001;
constexpr std::uint32_t kArmiesPicture = 0x180000;
constexpr std::uint32_t kRumblePicture = 0x180003;
// The room past the list's clip a header may end in: the stacked heights' rounding.
constexpr float kClipSlack = 1e-4F;

// The first `count` lines of marked-up `text`: the text up to its count-th line break (`<CR>`, `<CR2>`, `<CR3 f>` or
// `<CRM>`), so the tags before it still apply.
// @coney-only Coney's crop of the sub-item's text to the lines that fit; the original's text widget crops by height
std::string firstLines(std::string_view text, std::size_t count) { return {}; }

// The sub-item's label grey (178).
constexpr std::string_view kSubLabelGrey = "B2B2B2FF";
// The lines a sub-item's block is padded to.
constexpr std::size_t kSubItemLines = 5;

// String `id`, or nothing without strings.
// @coney-only a lookup that tolerates no strings (the tests')
std::string_view text(const GlobalStrings* strings, std::uint32_t id) { return {}; }

// "unlocked/total".
// @coney-only the "%d/%d" of the formats, shared by two ports
std::string fraction(const UnlockTally& tally) { return {}; }

// Whether `number` is one of the three special entries.
// @coney-only the 95 / 60 / 101 test the original repeats in each function
bool isSpecial(int number) { return {}; }

// The low 16 bits of a sprite word: the rectangle.
// @coney-only Coney's sprite-word split (the original's batch takes the whole word)
std::size_t rectOf(std::uint32_t word) { return {}; }

} // namespace

std::string formatPlayTime(const GlobalStrings* strings, std::uint32_t seconds, bool withDays) { return {}; }

ThumbBatch thumbBatchOf(int number) { return {}; }

bool isGameStatsListed(const LevelRecord& record) { return {}; }

graphics::Rgba areaColour(double area) { return {}; }

GameStatsData gatherGameStats(const LevelTable& levels, const UnlockRecords& unlocks, const SavedProgress& progress,
                              const GlobalStrings* strings) {
    return {};
}

std::string gameStatsSummary(const GameStatsData& data, const GlobalStrings* strings) { return {}; }

std::string gameStatsSubItemText(const GameStatsLevel& level, const GlobalStrings* strings) { return {}; }

// The text widget's set-up: the base size, the menus' grey and the headers' font.
GameStatsSubItem::GameStatsSubItem() {}

void GameStatsSubItem::setup(const GameStatsLevel& level, const GlobalStrings* strings, graphics::SpriteBatch* batch) {}

void GameStatsSubItem::setReveal(float reveal) {}

TextLayout GameStatsSubItem::fullLayout() const { return {}; }

float GameStatsSubItem::fullHeight() const { return {}; }

void GameStatsSubItem::moveTo(float x, float top, float shown) {}

void GameStatsSubItem::update(const GuiFrame& frame) {}

void GameStatsSubItem::render(const GuiCanvas& canvas) const {}

// The summary's and the usage line's places and fonts.
GameStatsScreen::GameStatsScreen() {}

graphics::Rgba GameStatsScreen::backgroundColour(std::size_t step, float share) { return {}; }

graphics::SpriteBatch* GameStatsScreen::thumbBatch(const GameStatsLevel& level) const { return {}; }

void GameStatsScreen::playCue(int cue) const {}

void GameStatsScreen::open(GameStatsData data, std::uint64_t nowMs) {}

void GameStatsScreen::close() {}

void GameStatsScreen::handle(MenuCommand command, std::uint64_t nowMs) {}

void GameStatsScreen::update(const GuiFrame& frame) {}

void GameStatsScreen::render(const GuiCanvas& canvas) const {}

} // namespace coney::gui
