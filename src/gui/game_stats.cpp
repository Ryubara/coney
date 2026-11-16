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
// The sub-item's label grey (178).
constexpr std::string_view kSubLabelGrey = "B2B2B2FF";
// The lines a sub-item's block is padded to.
constexpr std::size_t kSubItemLines = 5;

// String `id`, or nothing without strings.
// @coney-only a lookup that tolerates no strings (the tests')
std::string_view text(const GlobalStrings* strings, std::uint32_t id) {
    return strings != nullptr ? strings->get(id) : std::string_view{};
}

// "unlocked/total".
// @coney-only the "%d/%d" of the formats, shared by two ports
std::string fraction(const UnlockTally& tally) { return std::format("{}/{}", tally.unlocked, tally.total); }

// Whether `number` is one of the three special entries.
// @coney-only the 95 / 60 / 101 test the original repeats in each function
bool isSpecial(int number) { return number == 95 || number == 60 || number == 101; }

// The low 16 bits of a sprite word: the rectangle.
// @coney-only Coney's sprite-word split (the original's batch takes the whole word)
std::size_t rectOf(std::uint32_t word) { return word & 0xffffU; }

} // namespace

std::string formatPlayTime(const GlobalStrings* strings, std::uint32_t seconds, bool withDays) {
    using namespace game_stats_strings;
    // A count and its word: singular for 1.
    const auto part = [strings](std::uint32_t count, std::uint32_t singular) {
        return std::format("{} {}", count, text(strings, count == 1 ? singular : singular + 1));
    };
    std::uint32_t days = seconds / 86400;
    std::uint32_t hours = (seconds % 86400) / 3600;
    const std::uint32_t minutes = (seconds % 3600) / 60;
    // Without days, each day is 24 hours.
    if (!withDays) {
        hours += days * 24;
        days = 0;
    }
    if (days == 0 && hours == 0) {
        return part(minutes, kMinute);
    }
    if (days == 0) {
        return std::format("{}, {}", part(hours, kMinute + 2), part(minutes, kMinute));
    }
    return std::format("{}, {}, {}", part(days, kMinute + 4), part(hours, kMinute + 2), part(minutes, kMinute));
}

ThumbBatch thumbBatchOf(int number) {
    if (isSpecial(number)) {
        return ThumbBatch::Special;
    }
    constexpr std::array<int, 12> kB{2, 3, 5, 9, 11, 20, 31, 34, 51, 52, 54, 55};
    return std::ranges::find(kB, number) != kB.end() ? ThumbBatch::B : ThumbBatch::A;
}

bool isGameStatsListed(const LevelRecord& record) {
    const int number = static_cast<int>(record.number);
    if (record.values.at(LevelRecord::kMissionValue) == 0.0) {
        return false;
    }
    return !(number >= 61 && number <= 65) && !(number >= 102 && number <= 137);
}

graphics::Rgba areaColour(double area) {
    switch (static_cast<int>(area)) {
    case 1:
        return {132, 78, 37, 255};
    case 2:
        return {62, 98, 41, 255};
    case 3:
        return {42, 71, 141, 255};
    case 4:
        return {74, 49, 29, 255};
    default:
        // Coney's choice: black; the original sets only the alpha and leaves the colour as it was (not seen).
        return graphics::kBlack;
    }
}

GameStatsData gatherGameStats(const LevelTable& levels, const UnlockRecords& unlocks, const SavedProgress& progress,
                              const GlobalStrings* strings) {
    using namespace game_stats_strings;
    // The listed records, in level-table order.
    std::vector<std::size_t> listed;
    for (std::size_t index = 0; index < LevelTable::kCapacity; ++index) {
        if (const LevelRecord* record = levels.at(index); record != nullptr && isGameStatsListed(*record)) {
            listed.push_back(index);
        }
    }
    // Sorted by mission byte. Coney's choice: the level number breaks a tie, which the original's unstable sort leaves
    // open (the disc's bytes are distinct).
    const auto key = [&levels](std::size_t index) {
        const LevelRecord& record = *levels.at(index);
        return std::pair{record.values.at(LevelRecord::kMissionValue), record.number};
    };
    std::ranges::sort(listed, {}, key);

    GameStatsData data;
    data.counts = unlocks.countUnlocked(progress);
    // Coney's choice: fewer than three listed records count as none (the original's count would go negative).
    data.missionsListed = listed.size() > 3 ? listed.size() - 3 : 0;
    std::uint64_t seconds = progress.missionBests.at(0).total;
    for (const std::size_t index : listed) {
        const LevelRecord& record = *levels.at(index);
        GameStatsLevel level;
        level.index = index;
        level.number = static_cast<int>(record.number);
        level.mission = static_cast<std::uint8_t>(record.values.at(LevelRecord::kMissionValue));
        level.special = isSpecial(level.number);
        level.complete = unlocks.isLevelComplete(progress, static_cast<std::uint8_t>(level.number));
        if (level.mission < progress.missionBests.size()) {
            level.best = progress.missionBests.at(level.mission);
        }
        // Only the shown entries count: the complete levels, and the hub and the Rumble arenas always.
        if (!level.complete && level.number != 95 && level.number != 101) {
            continue;
        }
        seconds += level.best.total;
        if (!level.special) {
            ++data.missionsDone;
        }

        // The header: a special entry shows only its string, with nothing circled; a mission its name and its title's
        // number circled in the level's colour (purple when the number reads as 0).
        if (level.number == 95) {
            level.name = text(strings, kHub);
        } else if (level.number == 60) {
            level.name = text(strings, kArmies);
        } else if (level.number == 101) {
            level.name = text(strings, kRumbleArenas);
        } else {
            level.name = record.fourthName;
            const std::size_t colon = record.secondName.find(':');
            level.circled =
                colon != std::string::npos ? record.secondName.substr(0, std::min<std::size_t>(colon, 3)) : "";
            int number = 0;
            (void)std::from_chars(level.circled.data(), level.circled.data() + level.circled.size(), number);
            level.circleColour = number == 0 ? kPurpleRgba : areaColour(record.values.at(LevelRecord::kSubwayValue));
        }

        // The picture and the counts.
        level.pictureWord = static_cast<std::uint32_t>(record.values.at(LevelRecord::kPictureValue));
        if (level.number == 95) {
            level.pictureWord = kHubPicture;
        } else if (level.number == 60) {
            level.pictureWord = kArmiesPicture;
        } else if (level.number == 101) {
            level.pictureWord = kRumblePicture;
        }
        level.targets = unlocks.levelTargets(progress, level.number);
        data.levels.push_back(std::move(level));
    }
    // Coney's choice: the sum stops at the largest count (the original's would wrap).
    data.totalSeconds = static_cast<std::uint32_t>(std::min<std::uint64_t>(seconds, UINT32_MAX));
    return data;
}

std::string gameStatsSummary(const GameStatsData& data, const GlobalStrings* strings) {
    using namespace game_stats_strings;
    // One line: the grey label, the value's colour, a space and the value.
    const auto line = [strings](std::uint32_t label, std::string_view colour, std::string_view value) {
        return std::format("<COLOR {}>{}<COLOR {}> {}<CR>", kLabelGrey, text(strings, label), colour, value);
    };
    const UnlockCounts& counts = data.counts;
    const unsigned percent = counts.percent();
    const bool missionsComplete = data.missionsDone >= data.missionsListed;
    std::string out =
        std::format("<BIGFONT><COLOR {}><SIZE 1.6>{}<CR></BIGFONT><SIZE 1.2>", kTitleGrey, text(strings, kTitle));
    out += line(kCompletion, percent >= 100 ? kRed : kGold, std::format("{}%", percent));
    out +=
        line(kMissions, missionsComplete ? kRed : kGold, std::format("{}/{}", data.missionsDone, data.missionsListed));
    out += line(kGroupItems, counts.groupItems.complete() ? kRed : kPurple, fraction(counts.groupItems));
    out += line(kUnlocked, counts.records.complete() ? kRed : kGold, fraction(counts.records));
    // The time: the space before its colour, no line break after it.
    out += std::format("<COLOR {}>{} <COLOR {}>{}", kLabelGrey, text(strings, kTime), kGold,
                       formatPlayTime(strings, data.totalSeconds, false));
    return out;
}

std::string gameStatsSubItemText(const GameStatsLevel& level, const GlobalStrings* strings) {
    using namespace game_stats_strings;
    std::string out;
    std::size_t written = 0;
    const LevelTargets& targets = level.targets;
    // The score: with the goal in brackets while below it; the hub and the Rumble arenas only set the size.
    if (level.number == 95 || level.number == 101) {
        out += "<SIZE 1.2>";
    } else if (level.best.bestScore < targets.scoreTarget) {
        out += std::format("<SIZE 1.2><COLOR {}>{}<COLOR {}> {}<COLOR {}> ({} <COLOR {}>{}<COLOR {}>)<CR>",
                           kSubLabelGrey, text(strings, kScore), kGold, level.best.bestScore, kLabelGrey,
                           text(strings, kGoal), kRed, targets.scoreTarget, kLabelGrey);
        ++written;
    } else {
        out += std::format("<SIZE 1.2><COLOR {}>{}<COLOR {}> {}<CR>", kSubLabelGrey, text(strings, kScore), kRed,
                           level.best.bestScore);
        ++written;
    }
    // The three counts, each left out while its total is 0.
    const auto count = [&](std::uint32_t label, const UnlockTally& tally, std::string_view incomplete) {
        if (tally.total != 0) {
            out += std::format("<COLOR {}>{}<COLOR {}> {}<CR>", kSubLabelGrey, text(strings, label),
                               tally.complete() ? kRed : incomplete, fraction(tally));
            ++written;
        }
    };
    count(kGroupItems, targets.groupItems, kPurple);
    count(kCharacters, targets.characters, kGold);
    count(kArenas, targets.arenas, kGold);
    // The time, then line breaks up to five lines.
    out += std::format("<COLOR {}>{} <COLOR {}>{}", kSubLabelGrey, text(strings, kTime), kGold,
                       formatPlayTime(strings, level.best.total, false));
    ++written;
    for (; written < kSubItemLines; ++written) {
        out += "<CR>";
    }
    return out;
}

// The text widget's set-up: the base size, the menus' grey and the headers' font.
GameStatsSubItem::GameStatsSubItem() {
    m_text.setup(TextWidgetSetup{.scale = kTextSize, .colour = kMenuGrey, .fontSlot = CircledText::kFontSlot});
}

void GameStatsSubItem::setup(const GameStatsLevel& level, const GlobalStrings* strings, graphics::SpriteBatch* batch) {
    m_text.setText(gameStatsSubItemText(level, strings));
    m_picture.setBatch(batch);
    m_picture.setRect(rectOf(level.pictureWord));
}

void GameStatsSubItem::setReveal(float reveal) { m_reveal = std::clamp(reveal, 0.0F, 1.0F); }

TextLayout GameStatsSubItem::fullLayout() const {
    // The lines' advances come from the size alone, so no fonts are needed.
    return layoutText(m_text.text(), m_text.style(), {});
}

float GameStatsSubItem::fullHeight() const {
    // The text widget's height is its measure's bottom less its top, the bottom one line past the last line's centre
    // line (MessageHUD_Layout): Coney's layout ends at the last glyph's bottom, so the last line's gap is added.
    return fullLayout().height + graphics::fontMetrics(kLineSize).lineGap;
}

void GameStatsSubItem::moveTo(float x, float top, float reveal) {
    const float full = fullHeight();
    const float shown = std::clamp(reveal, 0.0F, 1.0F);
    const float height = full * shown;
    m_drawn = shown > 0.0F;
    // The picture centred on the block's x and on its shown height, the column's width wide, its texture rectangle cut
    // to the shown share.
    m_picture.setup(BaseWidgetSetup{.x = x,
                                    .y = top + (height / 2.0F),
                                    .height = height,
                                    .width = graphics::OverlayCamera::guiWidthToOverlay(kColumnWidth),
                                    .rectShare = shown});
    // The block is as wide as the column (its text widget is set up with width 0), so its left edge is half a column
    // left of x; the text starts kTextGap past the column, its first line half a size-1.0 line below the top (the
    // original measures `Font_Size(1.0)` here, not the text's `<SIZE 1.2>`).
    TextStyle& style = m_text.style();
    const float left = x - (kColumnWidth / 2.0F);
    style.x = left + kColumnWidth + kTextGap;
    style.y = top + (graphics::fontMetrics(kLayoutFontSize).height / 2.0F);
    // The text takes the same reveal and is wiped from the top, line by line, to the depth of its height less a line
    // gap of its widget's size (ScrollingTextWidget_Update's crop).
    m_text.setReveal(shown);
    m_text.setRevealDepth(std::max(full - graphics::fontMetrics(kTextSize).lineGap, 0.0F));
    m_text.setVisible(m_drawn);
}

void GameStatsSubItem::update(const GuiFrame& frame) { m_text.update(frame); }

void GameStatsSubItem::render(const GuiCanvas& canvas) const {
    if (!visible() || !m_drawn) {
        return;
    }
    m_picture.render(canvas);
    m_text.render(canvas);
}

GameStatsLayout GameStatsLayout::forFlags(bool flag02, bool widescreen, bool flag20) {
    // Flag 0x02 first, with or without 16:9.
    if (flag02 && widescreen) {
        return GameStatsLayout{.textX = -0.15F,
                               .summaryY = 0.14F,
                               .usageY = 0.86F,
                               .backgroundWidth = 1.55F,
                               .backgroundHeight = 0.95F,
                               .headerWidth = 0.43F,
                               .headerHeight = 0.078F,
                               .circleWidth = 0.051F,
                               .circleHeight = 0.077F,
                               .listX = 0.07F,
                               .listY = 0.46F,
                               .clipHeight = 0.4F,
                               .stepDivisor = CircledText::kStepDivisor02};
    }
    if (flag02) {
        return GameStatsLayout{.textX = 0.02F,
                               .summaryY = 0.16F,
                               .usageY = 0.82F,
                               .backgroundWidth = 1.1F,
                               .backgroundHeight = 0.8F,
                               .headerWidth = 0.3F,
                               .headerHeight = 0.07F,
                               .circleWidth = 0.05F,
                               .circleHeight = 0.077F,
                               .listX = 0.17F,
                               .listY = 0.445F,
                               .clipHeight = 0.38F,
                               .stepDivisor = CircledText::kStepDivisor02};
    }
    // Then progressive, whatever its shape: the circles keep their static size, the default mode's.
    if (flag20) {
        return GameStatsLayout{.textX = -0.05F,
                               .summaryY = 0.08F,
                               .usageY = 0.9F,
                               .backgroundWidth = 0.74F,
                               .backgroundHeight = 0.74F,
                               .headerWidth = 0.3F,
                               .headerHeight = 0.09F,
                               .circleWidth = 0.065F,
                               .circleHeight = 0.11F,
                               .listX = 0.1F,
                               .listY = 0.45F,
                               .clipHeight = 0.5F,
                               .stepDivisor = CircledText::kStepDivisor};
    }
    // Then 16:9; else the default.
    if (widescreen) {
        return GameStatsLayout{.textX = -0.07F,
                               .summaryY = 0.04F,
                               .usageY = 0.94F,
                               .backgroundWidth = 1.45F,
                               .backgroundHeight = 1.2F,
                               .headerWidth = 0.43F,
                               .headerHeight = 0.09F,
                               .circleWidth = 0.081F,
                               .circleHeight = 0.11F,
                               .listX = 0.145F,
                               .listY = 0.45F,
                               .clipHeight = 0.5F,
                               .stepDivisor = CircledText::kStepDivisor};
    }
    return GameStatsLayout{};
}

graphics::Rgba GameStatsScreen::backgroundColour(std::size_t step, float share) {
    const graphics::Rgba from = kColours.at(step % kColours.size());
    const graphics::Rgba to = kColours.at((step + 1) % kColours.size());
    // Each channel a x (1 - share) + b x share, the fraction dropped.
    const auto mix = [share](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>((static_cast<float>(a) * (1.0F - share)) + (static_cast<float>(b) * share));
    };
    return graphics::Rgba{mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a)};
}

graphics::SpriteBatch* GameStatsScreen::thumbBatch(const GameStatsLevel& level) const {
    switch (thumbBatchOf(level.number)) {
    case ThumbBatch::A:
        return m_batches.thumbsA;
    case ThumbBatch::B:
        return m_batches.thumbsB;
    case ThumbBatch::Special:
        break;
    }
    // Coney's choice: the specials' pictures come from the header batch, the same record 24 (the original makes a
    // third batch over it).
    return m_batches.header;
}

void GameStatsScreen::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void GameStatsScreen::open(GameStatsData data, std::uint64_t nowMs) {
    m_data = std::move(data);
    m_open = true;
    m_closed = false;
    // The colour cycle starts at red now (the summary's set-up stamps it).
    m_stepStartMs = nowMs;
    m_colourStep = 0;
    m_background.setBatch(m_batches.background);
    // The background: the layout's width (a GUI width, converted) by its height, as each update sizes it.
    m_background.setup(BaseWidgetSetup{.x = 0.5F,
                                       .y = 0.5F,
                                       .height = m_layout.backgroundHeight,
                                       .width = graphics::OverlayCamera::guiWidthToOverlay(m_layout.backgroundWidth),
                                       .colour = kColours.at(0)});
    // The summary and the usage line at the layout's places.
    m_summary.setup(TextWidgetSetup{
        .x = m_layout.textX, .y = m_layout.summaryY, .colour = kMenuGrey, .fontSlot = CircledText::kFontSlot});
    // Coney's choice: a text widget in the text font draws the usage line (the original's UsageInfo).
    m_usage.setup(
        TextWidgetSetup{.x = m_layout.textX, .y = m_layout.usageY, .colour = kMenuGrey, .fontSlot = kTextFontSlot});
    // Coney's choice: the summary's bottom and the usage line's top, which the original also stores, are not kept
    // (nothing reads them).
    m_summary.setText(gameStatsSummary(m_data, m_strings));
    // Coney's choice: the usage text goes to a text widget (the original's UsageInfo).
    m_usage.setText(m_strings != nullptr ? m_strings->get(game_stats_strings::kUsage) : std::string_view{});

    // The list: one entry per mission, its header with its sub-item as the description.
    m_headers.clear();
    m_subItems.clear();
    for (const GameStatsLevel& level : m_data.levels) {
        auto header = std::make_unique<CircledText>();
        // Back and circles are both `0x180002`; the back is made invisible. The list's x is the header's centre (the
        // original's CircledText is placed by its centre); Coney's CircledText takes the left edge.
        header->setBack(m_batches.header, kHeaderRect);
        header->setCircleSprite(m_batches.header, kHeaderRect);
        header->setup(CircledTextSetup{.x = m_layout.listX - (m_layout.headerWidth / 2.0F),
                                       .y = m_layout.listY,
                                       .width = m_layout.headerWidth,
                                       .height = m_layout.headerHeight,
                                       .circleWidth = m_layout.circleWidth,
                                       .circleHeight = m_layout.circleHeight,
                                       .stepDivisor = m_layout.stepDivisor});
        header->setBackColour(graphics::Rgba{0, 0, 0, 0});
        header->setText(level.name);
        // A special entry sets only its text: no circled characters, no colours.
        if (!level.special) {
            header->setCircled(level.circled);
            header->setCharColour(graphics::kBlack);
            header->setCircleColour(level.circleColour);
        }
        m_headers.push_back(std::move(header));
        auto item = std::make_unique<GameStatsSubItem>();
        item->setup(level, m_strings, thumbBatch(level));
        m_subItems.push_back(std::move(item));
    }
    m_list.setup(ScrollingMenuSetup{.visible = kVisible, .playCue = m_playCue}, m_data.levels.size());
    m_window.setup(
        ScrollingWindowSetup{
            .maxVisible = kVisible, .clipHeight = m_layout.clipHeight, .step = kRevealStep, .evenRows = true},
        m_data.levels.size());
    m_list.takeFocus(m_input, nowMs);
    m_lastCommandMs.reset();
    // The first entry is focused, its description open at once and given the focus too, which it keeps until a move.
    m_focused = 0;
    m_focusMs = nowMs;
    m_firstBlockFocused = !m_subItems.empty();
    if (!m_subItems.empty()) {
        m_subItems.front()->setReveal(1.0F);
    }
}

void GameStatsScreen::close() {
    m_open = false;
    m_headers.clear();
    m_subItems.clear();
}

void GameStatsScreen::handle(MenuCommand command, std::uint64_t nowMs) {
    m_lastCommandMs = nowMs;
    switch (command) {
    case MenuCommand::Back:
        m_closed = true;
        return;
    case MenuCommand::Accept:
        return;
    case MenuCommand::Up:
    case MenuCommand::Down:
    case MenuCommand::Left:
    case MenuCommand::Right: {
        const std::size_t before = m_list.selected();
        (void)m_list.handle(command, nowMs);
        const std::size_t selected = m_list.selected();
        if (selected == before) {
            return;
        }
        // A new selection takes the focus now, from both parts of the entry before it, and the window may scroll.
        if (command == MenuCommand::Down) {
            m_window.moveDown(selected);
        } else {
            m_window.moveUp(selected);
        }
        m_focused = selected;
        m_focusMs = nowMs;
        m_firstBlockFocused = false;
        return;
    }
    }
}

float GameStatsScreen::layOutEntry(std::size_t index, std::optional<float> offset, float reveal,
                                   const GuiFrame& frame) {
    CircledText& header = *m_headers.at(index);
    GameStatsSubItem& item = *m_subItems.at(index);
    // The header at the list's place plus the offset, with the entry's reveal.
    header.setReveal(reveal);
    if (offset) {
        header.moveTo(m_layout.listY + *offset);
    }
    header.setFocused(index == m_list.selected());
    header.update(frame);
    // The block opens a step an update while its header shows whole and it has the focus (the first entry's from the
    // start, any entry's from kRevealDelayMs after it took the focus), with cue 0xd as it starts; otherwise it closes.
    const bool focused =
        (index == 0 && m_firstBlockFocused) || (index == m_focused && frame.timeMs - m_focusMs >= kRevealDelayMs);
    if (header.reveal() == 1.0F && focused) {
        if (item.reveal() == 0.0F) {
            playCue(kRevealCue);
        }
        if (item.reveal() < 1.0F) {
            item.setReveal(item.reveal() + kRevealStep);
        }
    } else if (item.reveal() > 0.0F) {
        item.setReveal(item.reveal() - kRevealStep);
    }
    // The block from the header's shown bottom less kRowOverlap, on the list's x.
    const float headerY = header.text().style().y;
    item.moveTo(m_layout.listX, headerY + (m_layout.headerHeight * reveal / 2.0F) - kRowOverlap, item.reveal());
    item.update(frame);
    // The entry's height at reveal 1: the header's, and the shown block's less the overlap while it shows.
    const float block = item.height();
    return m_layout.headerHeight + (block > 0.0F && item.reveal() > 0.0F ? block - kRowOverlap : 0.0F);
}

void GameStatsScreen::update(const GuiFrame& frame) {
    if (!m_open) {
        return;
    }
    // The colour: a step moves on (and starts again from now) on the first frame more than kColourStepMs after it
    // began, showing its first colour; otherwise the colour is blended through the step.
    const std::uint64_t inStep = frame.timeMs - m_stepStartMs;
    float share = 0.0F;
    if (inStep > kColourStepMs) {
        m_colourStep = (m_colourStep + 1) % kColours.size();
        m_stepStartMs = frame.timeMs;
    } else {
        share = static_cast<float>(inStep) / static_cast<float>(kColourStepMs);
    }
    m_background.setColour(backgroundColour(m_colourStep, share));
    m_summary.update(frame);

    // The list: the pad's command first, read only while the window is still and kInputGapMs after the last command;
    // then the window's update, which lays the entries out.
    const bool waited = !m_lastCommandMs || frame.timeMs > *m_lastCommandMs + kInputGapMs;
    if (!m_closed && frame.pad != nullptr && !m_window.scrolling() && waited) {
        if (const std::optional<MenuCommand> command = m_input.dispatch(*frame.pad, frame.timeMs)) {
            handle(*command, frame.timeMs);
        }
    }
    m_window.update(ScrollingWindow::Entries{
        .layOut = [this, &frame](std::size_t index, std::optional<float> offset,
                                 float reveal) { return layOutEntry(index, offset, reveal, frame); },
        .focused = [this](std::size_t index) { return index == m_list.selected(); },
    });
    // Only the window's drawn entries are drawn.
    const std::size_t first = m_window.first();
    for (std::size_t i = 0; i < m_headers.size(); ++i) {
        const bool drawn = i >= first && i < first + m_window.drawn();
        m_headers.at(i)->setVisible(drawn);
        m_subItems.at(i)->setVisible(drawn);
    }
    m_usage.update(frame);
}

void GameStatsScreen::render(const GuiCanvas& canvas) const {
    if (!m_open) {
        return;
    }
    m_background.render(canvas);
    m_summary.render(canvas);
    // The window's drawn entries, each header and then its block.
    for (std::size_t i = m_window.first(); i < m_window.first() + m_window.drawn(); ++i) {
        m_headers.at(i)->render(canvas);
        m_subItems.at(i)->render(canvas);
    }
    m_usage.render(canvas);
}

} // namespace coney::gui
