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
std::string firstLines(std::string_view text, std::size_t count) {
    std::size_t lines = 1;
    for (std::size_t open = text.find('<'); open != std::string_view::npos; open = text.find('<', open + 1)) {
        const std::size_t close = text.find('>', open);
        if (close == std::string_view::npos) {
            break;
        }
        // The tag's name runs to its first space or its `>`, as the layout reads it.
        const std::string_view inside = text.substr(open + 1, close - open - 1);
        const std::optional<MarkupTag> tag = findMarkupTag(inside.substr(0, inside.find(' ')));
        const bool lineBreak =
            tag == MarkupTag::Cr || tag == MarkupTag::Cr2 || tag == MarkupTag::Cr3 || tag == MarkupTag::Crm;
        if (!lineBreak) {
            continue;
        }
        if (lines == count) {
            return std::string(text.substr(0, open));
        }
        ++lines;
    }
    return count == 0 ? std::string{} : std::string(text);
}

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

float GameStatsSubItem::fullHeight() const { return fullLayout().height; }

void GameStatsSubItem::moveTo(float x, float top, float shown) {
    const TextLayout whole = fullLayout();
    const float full = whole.height;
    const float height = std::clamp(shown, 0.0F, 1.0F) * full;
    m_drawn = height > 0.0F;
    // The picture centred on the block's x and on its shown height, the column's width wide. Coney's choice: drawn
    // whole in the shown height (the original cuts its texture rectangle by the reveal).
    m_picture.setup(BaseWidgetSetup{.x = x,
                                    .y = top + (height / 2.0F),
                                    .height = height,
                                    .width = graphics::OverlayCamera::guiWidthToOverlay(kColumnWidth)});
    // The block is as wide as the column (its text widget is set up with width 0), so its left edge is half a column
    // left of x; the text starts kTextGap past the column, its first line half a size-1.0 line below the top (the
    // original measures `Font_Size(1.0)` here, not the text's `<SIZE 1.2>`). Only the lines that fit the shown height.
    TextStyle& style = m_text.style();
    const float left = x - (kColumnWidth / 2.0F);
    style.x = left + kColumnWidth + kTextGap;
    style.y = top + (graphics::fontMetrics(kLayoutFontSize).height / 2.0F);
    // Coney's choice: the lines that fit the shown height are drawn (the original's text widget crops by height).
    const auto fit =
        static_cast<std::size_t>((static_cast<float>(whole.lines) * height / std::max(full, 1e-6F)) + 1e-3F);
    m_shownLines = fit;
    m_text.setVisible(fit > 0);
}

void GameStatsSubItem::update(const GuiFrame& frame) { m_text.update(frame); }

void GameStatsSubItem::render(const GuiCanvas& canvas) const {
    if (!visible() || !m_drawn) {
        return;
    }
    m_picture.render(canvas);
    // The text's lines that fit, laid out as its widget would.
    if (m_text.visible() && canvas.fonts && canvas.textBatch) {
        addTextSprites(layoutText(firstLines(m_text.text(), m_shownLines), m_text.style(), canvas.fonts),
                       canvas.textBatch);
    }
}

// The summary's and the usage line's places and fonts.
GameStatsScreen::GameStatsScreen() {
    m_summary.setup(
        TextWidgetSetup{.x = kSummaryX, .y = kSummaryY, .colour = kMenuGrey, .fontSlot = CircledText::kFontSlot});
    // Coney's choice: a text widget in the text font draws the usage line (the original's UsageInfo).
    m_usage.setup(TextWidgetSetup{.x = kUsageX, .y = kUsageY, .colour = kMenuGrey, .fontSlot = kTextFontSlot});
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
    // The background: kBackgroundWidth (a GUI width, converted) by kBackgroundHeight, as each update sizes it.
    m_background.setup(BaseWidgetSetup{.x = 0.5F,
                                       .y = 0.5F,
                                       .height = kBackgroundHeight,
                                       .width = graphics::OverlayCamera::guiWidthToOverlay(kBackgroundWidth),
                                       .colour = kColours.at(0)});
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
        header->setup(CircledTextSetup{.x = kListX - (kHeaderWidth / 2.0F),
                                       .y = kListY,
                                       .width = kHeaderWidth,
                                       .height = kHeaderHeight,
                                       .circleWidth = kCircleWidth,
                                       .circleHeight = kCircleHeight});
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
    m_list.takeFocus(m_input, nowMs);
    // The first entry is focused with its description open at once.
    m_focused = 0;
    m_focusMs = nowMs;
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
    switch (command) {
    case MenuCommand::Back:
        m_closed = true;
        return;
    case MenuCommand::Accept:
        return;
    case MenuCommand::Up:
    case MenuCommand::Down:
    case MenuCommand::Left:
    case MenuCommand::Right:
        (void)m_list.handle(command, nowMs);
        // A new selection takes the focus now.
        if (m_list.selected() != m_focused) {
            m_focused = m_list.selected();
            m_focusMs = nowMs;
        }
        return;
    }
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
    m_usage.update(frame);
    // The descriptions: the focused one opens once the delay has run, the others close, a step a frame.
    for (std::size_t i = 0; i < m_subItems.size(); ++i) {
        GameStatsSubItem& item = *m_subItems.at(i);
        if (i != m_focused) {
            item.setReveal(item.reveal() - kRevealStep);
            continue;
        }
        // Coney's choice: the focused one holds until the delay has run (so the first, open from the start, stays
        // open); the description's own focus, which the original's menu input waits on, is not modelled.
        if (frame.timeMs - m_focusMs >= kRevealDelayMs && item.reveal() < 1.0F) {
            if (item.reveal() == 0.0F) {
                playCue(kRevealCue);
            }
            item.setReveal(item.reveal() + kRevealStep);
        }
    }
    // The window's entries, stacked down from the list's place: each header centred where the stack has reached, its
    // block from the header's bottom less kRowOverlap, the next entry one header and the shown block (less the overlap)
    // further down. The stacked heights are clipped at kClipHeight.
    const std::size_t first = m_list.firstVisible();
    float centre = kListY;
    float used = 0.0F;
    for (std::size_t i = 0; i < m_headers.size(); ++i) {
        CircledText& header = *m_headers.at(i);
        GameStatsSubItem& item = *m_subItems.at(i);
        const bool inWindow = i >= first && i < first + m_list.visibleCount();
        // Coney's choice: the window jumps to keep the cursor in its middle row (the original scrolls with a reveal),
        // and a header that ends on the clip fits (a little room for the sum's rounding).
        const bool shown = inWindow && used + kHeaderHeight <= kClipHeight + kClipSlack;
        header.setVisible(shown);
        item.setVisible(shown);
        if (!shown) {
            continue;
        }
        header.moveTo(centre);
        header.setFocused(i == m_list.selected());
        header.update(frame);
        // The block under its header. Coney's choice: a block the clip cuts is cut at the clip (the original reveals
        // the last item in part).
        const float full = item.fullHeight();
        const float room = kClipHeight - used - kHeaderHeight + kRowOverlap;
        const float shownShare = full > 0.0F ? std::clamp(std::min(item.reveal(), room / full), 0.0F, 1.0F) : 0.0F;
        item.moveTo(kListX, centre + (kHeaderHeight / 2.0F) - kRowOverlap, shownShare);
        item.update(frame);
        const float shownHeight = full * shownShare;
        const float step = kHeaderHeight + (shownHeight > 0.0F ? shownHeight - kRowOverlap : 0.0F);
        centre += step;
        used += step;
    }
    if (!m_closed && frame.pad != nullptr) {
        if (const std::optional<MenuCommand> command = m_input.dispatch(*frame.pad, frame.timeMs)) {
            handle(*command, frame.timeMs);
        }
    }
}

void GameStatsScreen::render(const GuiCanvas& canvas) const {
    if (!m_open) {
        return;
    }
    m_background.render(canvas);
    m_summary.render(canvas);
    for (std::size_t i = 0; i < m_headers.size(); ++i) {
        if (m_headers.at(i)->visible()) {
            m_headers.at(i)->render(canvas);
            m_subItems.at(i)->render(canvas);
        }
    }
    m_usage.render(canvas);
}

} // namespace coney::gui
