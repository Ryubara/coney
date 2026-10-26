// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/sprite_batch.h"
#include "gui/base_widget.h"
#include "gui/circled_text.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/scrolling_menu.h"
#include "gui/text_widget.h"
#include "gui/widget.h"
#include "warriors/level_table.h"
#include "warriors/profile_record.h"
#include "warriors/unlock_records.h"

namespace coney::gui {

/// The statistics screen's strings (`GSTRING.HUD` ids).
namespace game_stats_strings {
inline constexpr std::uint32_t kTitle = 0x60;        ///< The summary's title.
inline constexpr std::uint32_t kCompletion = 0x61;   ///< The completion percentage's label.
inline constexpr std::uint32_t kUnlocked = 0x62;     ///< The counted records' label.
inline constexpr std::uint32_t kMissions = 0x63;     ///< The missions' label.
inline constexpr std::uint32_t kGroupItems = 0x64;   ///< The group-1 items' label.
inline constexpr std::uint32_t kTime = 0x66;         ///< The time's label.
inline constexpr std::uint32_t kMinute = 0x67;       ///< The time's words: minute, minutes, hour, hours, day, days.
inline constexpr std::uint32_t kScore = 0x6d;        ///< A mission's score label.
inline constexpr std::uint32_t kGoal = 0x6e;         ///< A mission's score goal label.
inline constexpr std::uint32_t kCharacters = 0x6f;   ///< A mission's type-4 records' label.
inline constexpr std::uint32_t kArenas = 0x70;       ///< A mission's type-2 records' label.
inline constexpr std::uint32_t kHub = 0x73;          ///< Level 95's header text.
inline constexpr std::uint32_t kArmies = 0x74;       ///< Level 60's header text.
inline constexpr std::uint32_t kRumbleArenas = 0x75; ///< Level 101's header text.
inline constexpr std::uint32_t kUsage = 0x19;        ///< The usage line.
} // namespace game_stats_strings

/// Formats `seconds` (seconds dropped) as "<m> <word>" below an hour, "<h> <word>, <m> <word>" from an hour, or (with
/// `withDays`, from a day) "<d> <word>, <h> <word>, <m> <word>", each word the singular for 1, else the plural (strings
/// 0x67-0x6c). Without `withDays` (both callers) the days count as 24 hours each. The minutes always show after the
/// hours, 0 included.
/// @orig 0x00214f58 GameStats_FormatTime (GameStats.cpp)
[[nodiscard]] std::string formatPlayTime(const GlobalStrings* strings, std::uint32_t seconds, bool withDays);

/// Which sprite batch a level's picture comes from (`GameStats_PickThumbBatch`).
enum class ThumbBatch : std::uint8_t {
    A,       ///< `+0x6c`, sheet-table record 22: most levels.
    B,       ///< `+0x70`, record 23: levels 2, 3, 5, 9, 11, 20, 31, 34, 51, 52, 54 and 55.
    Special, ///< `+0x74`, record 24: levels 95, 60 and 101.
};

/// The batch of level `number`'s picture.
/// @orig 0x00215490 GameStats_PickThumbBatch (GameStats.cpp)
[[nodiscard]] ThumbBatch thumbBatchOf(int number);

/// One mission of the statistics list: what its header and sub-item show.
struct GameStatsLevel {
    std::size_t index = 0;         ///< The level record's index.
    int number = 0;                ///< The level number.
    std::uint8_t mission = 0;      ///< The record's mission byte (`+0x0c`): the slot of its best record.
    std::string name;              ///< The header's text: the level's name, or strings 0x73-0x75 for the specials.
    std::string circled;           ///< The header's circled characters: the title's number, three at most.
    graphics::Rgba circleColour{}; ///< The level's colour, purple when its number is 0.
    std::uint32_t pictureWord = 0; ///< The picture's sprite word (record `+0x80`, or the specials' own).
    bool special = false;          ///< Levels 95, 60 and 101.
    bool complete = false;         ///< Whether the level is complete (the specials 95 and 101 are always shown).
    MissionBest best;              ///< The level's best record (score, time).
    LevelTargets targets;          ///< Its unlockable counts.
};

/// Everything the statistics screen shows, gathered when it opens (`GameStats_Init`).
struct GameStatsData {
    std::vector<GameStatsLevel> levels; ///< The list's levels, sorted; only the shown ones.
    UnlockCounts counts;                ///< The summary's counts.
    std::size_t missionsDone = 0;       ///< Shown (complete) levels, less the special entries.
    std::size_t missionsListed = 0;     ///< Listed levels less the three special entries.
    std::uint32_t totalSeconds = 0;     ///< The shown entries' times plus slot 0's.
};

/// Whether level record `record` belongs in the list: its mission byte is set and its number is not 61-65 or 102-137.
/// @orig 0x00215ca0 GameStats_IsLevelListed (GameStats.cpp)
[[nodiscard]] bool isGameStatsListed(const LevelRecord& record);

/// The colour of a level whose record has area byte `area` (record `+0x7c`): 1 (132, 78, 37), 2 (62, 98, 41),
/// 3 (42, 71, 141), 4 (74, 49, 29). For any other value the original leaves its output unwritten (not seen); Coney
/// gives black.
/// @orig 0x0041f298 LevelRecord_GetAreaColour (unknown)
[[nodiscard]] graphics::Rgba areaColour(double area);

/// Gathers the screen's data from the level table, the unlockables and the profile's bests: the listed levels sorted
/// by their mission byte (`0x004ed068`; the disc's bytes are distinct, the level number only breaks a tie), each kept
/// when it is complete, 95 and 101 always.
/// @orig 0x00215da0 GameStats_CollectLevels (GameStats.cpp)
/// @orig 0x00215ce8 GameStats_AddLevel (GameStats.cpp)
[[nodiscard]] GameStatsData gatherGameStats(const LevelTable& levels, const UnlockRecords& unlocks,
                                            const SavedProgress& progress, const GlobalStrings* strings);

/// The summary's marked-up text (format `0x00559f98`): the title (size 1.6, big font, grey 178), then five lines in
/// size 1.2, each a grey (80, 80, 80) label, the value's colour, a space and the value: the completion percentage
/// ("N%"), missions, group-1 items and counted records ("a/b"), and the total time, which has the space before its
/// colour and no line break after it. A value is red (134, 26, 26) when complete, else gold (128, 100, 0), purple
/// (106, 65, 131) for the group-1 line; the time is gold.
/// @orig 0x00215150 GameStats_BuildSummary (GameStats.cpp)
[[nodiscard]] std::string gameStatsSummary(const GameStatsData& data, const GlobalStrings* strings);

/// A mission's marked-up block (format `0x0055a3a0`), five lines in size 1.2 with grey 178 labels: the score (gold,
/// then the goal's label and the goal in red inside grey 80 brackets while below it; red alone otherwise; none for 95
/// and 101), the group-1 items (purple until complete), the characters and the arenas (gold until complete; red when
/// complete), each "label a/b" and left out when its total is 0, then the time (gold); line breaks pad the block to
/// five lines.
/// @orig 0x00216eb0 GameStatsSubItem_BuildText (GameStatsSubItem.cpp)
[[nodiscard]] std::string gameStatsSubItemText(const GameStatsLevel& level, const GlobalStrings* strings);

/// The sprite batches the screen draws with; any may be null (that part is not drawn).
struct GameStatsBatches {
    graphics::SpriteBatch* background = nullptr; ///< Record 12 (`+0x64`).
    graphics::SpriteBatch* header = nullptr;     ///< Record 24 (`+0x68`): the headers' backs and circles, the specials.
    graphics::SpriteBatch* thumbsA = nullptr;    ///< Record 22 (`+0x6c`).
    graphics::SpriteBatch* thumbsB = nullptr;    ///< Record 23 (`+0x70`).
};

/// One mission's block (our name `GameStatsSubItem`, `0x90` bytes): its header's description in the list, a picture
/// column on the left and the text right of it, both opened by the reveal.
///
/// - **Layout** (`GameStatsSubItem_Layout`): the block is placed by its x centre and its top; it is kColumnWidth wide
///   (its text widget has width 0). The picture (white) is centred on x and on the shown height, kColumnWidth wide; the
///   text starts kTextGap past the column, its first line half a size-1.0 line below the top. The block is as high as
///   the text x the reveal (`GameStatsSubItem_GetScaledRect`).
///
/// Coney's choices: the picture is drawn whole in the shown height (the original cuts its texture by the reveal); the
/// text shows the lines that fit the shown height (its share of the lines, rounded down).
///
/// Research: docs/research/frontend.md#game-stats
class GameStatsSubItem : public Widget {
  public:
    /// The picture column's width (`GameStatsSubItem_Setup`'s 0.35) and the text's gap after it.
    static constexpr float kColumnWidth = 0.35F;
    static constexpr float kTextGap = 0.02F;
    /// The text's base size; its `<SIZE 1.2>` makes the lines 1.2.
    static constexpr float kTextSize = 1.0F;
    /// The font size whose half line puts the text's first line below the block's top (`Font_Size(1.0)` in the
    /// layout).
    static constexpr float kLayoutFontSize = 1.0F;

    /// The text widget set up at the base size, the menus' grey and the headers' font.
    /// @coney-only Coney's construction (the original sets the text up in GameStatsSubItem_Setup)
    GameStatsSubItem();

    /// Sets the block up for `level`: its text and its picture from `batch` (null draws none).
    /// @orig 0x00217350 GameStatsSubItem_Setup (GameStatsSubItem.cpp)
    void setup(const GameStatsLevel& level, const GlobalStrings* strings, graphics::SpriteBatch* batch);
    /// The share shown, clamped to 0-1.
    /// @coney-only the reveal setter of the original's widget base, as Coney's widgets take it
    void setReveal(float reveal);
    /// The share shown.
    [[nodiscard]] float reveal() const { return m_reveal; }
    /// The text's whole height (GUI units; the fonts do not change it).
    /// @coney-only the text widget's height (`+0x38`), measured by Coney's layout
    [[nodiscard]] float fullHeight() const;
    /// The shown height: the text's height x the reveal.
    /// @orig 0x00217730 GameStatsSubItem_GetScaledRect (GameStatsSubItem.cpp)
    [[nodiscard]] float height() const { return fullHeight() * m_reveal; }
    /// Places the block with its centre at GUI x `x` and its top at `top`, showing `shown` of its height (the reveal,
    /// or less where the list's clip cuts it).
    /// @orig 0x00217788 GameStatsSubItem_Layout (GameStatsSubItem.cpp)
    void moveTo(float x, float top, float shown);

    /// The block's text.
    [[nodiscard]] const TextWidget& text() const { return m_text; }
    /// The picture.
    [[nodiscard]] const BaseWidget& picture() const { return m_picture; }
    /// Whether the last moveTo() left anything to draw.
    [[nodiscard]] bool drawn() const { return m_drawn; }

    /// The text's frame.
    /// @coney-only Coney's frame step for the text widget
    void update(const GuiFrame& frame) override;
    /// The picture, then the text, when anything is shown.
    /// @coney-only the sub-item's render as Coney's widgets draw (the original's slot +0x38 is not traced)
    void render(const GuiCanvas& canvas) const override;

  private:
    // The text laid out whole, without fonts: its height and line count.
    // @coney-only Coney's text measure
    [[nodiscard]] TextLayout fullLayout() const;

    BaseWidget m_picture{nullptr, 0};
    TextWidget m_text;
    float m_reveal = 0.0F;
    bool m_drawn = false; // whether moveTo() left anything to draw
};

/// The statistics screen (`GUI/GameStats.cpp`, game mode 0x13's widget): the colour-cycling background, the summary,
/// the list of complete missions and the usage line. Back closes it; accept does nothing; up and down move through the
/// list.
///
/// - **List** (`GameStats_BuildMissionList`): a `ScrollingMenu` at (0.185, 0.45) with one entry per mission, a window
///   of at most kVisible entries stacked down from there, their heights clipped at kClipHeight. An entry is a
///   `CircledText` header (kHeaderWidth x kHeaderHeight, circles kCircleWidth x kCircleHeight, back and circles both
///   record 24 rectangle 2, the back invisible, the characters black, the circles in the area's colour; the special
///   entries only a text) with the mission's `GameStatsSubItem` as its description.
/// - **Stacking** (`ScrollingMenu_Update`, `ScrollingMenuItem_Update` / `_GetRect`): the first header is centred on the
///   list's place, x and y; a block starts at its header's bottom less kRowOverlap (two screen lines) and is centred
///   on the same x; the next header is one header and the shown block, less the overlap, below.
/// - **Descriptions** (`ScrollingMenuItem_Update`): the focused entry's opens kRevealDelayMs after it took the focus,
///   by kRevealStep a frame (cue `0xd` as it starts); every other closes by the same step. The first entry's starts
///   open. Coney's reading: the focused one holds its reveal until the delay has run (so the first stays open).
///
/// Coney's choices where the page is silent: the window keeps the cursor in its middle row and jumps (the original
/// scrolls with a reveal); a description the clip cuts is cut at the clip,
/// and a header the clip would cut is hidden with the entries below it (the original reveals the last item in part).
///
/// Research: docs/research/frontend.md#game-stats, docs/research/frontend.md#fn-gamestats
class GameStatsScreen : public Widget {
  public:
    /// The background's sprite (word `0xc0005`: record 12, rectangle 5), size and depth.
    static constexpr std::uint32_t kBackgroundRecord = 12;
    static constexpr std::size_t kBackgroundRect = 5;
    static constexpr float kBackgroundWidth = 1.15F;
    static constexpr float kBackgroundHeight = 1.1F;
    static constexpr float kBackgroundDepth = 8000.0F;
    /// The headers' back and circle sprite (word `0x180002`) and the specials' pictures' record; the headers' depth.
    static constexpr std::uint32_t kHeaderRecord = 24;
    static constexpr std::size_t kHeaderRect = 2;
    static constexpr float kHeaderDepth = 8700.0F;
    /// The pictures' records and depth.
    static constexpr std::uint32_t kThumbsARecord = 22;
    static constexpr std::uint32_t kThumbsBRecord = 23;
    static constexpr float kThumbDepth = 8500.0F;
    /// One colour step of the background, in milliseconds (`0x0051011c`).
    static constexpr std::uint64_t kColourStepMs = 5000;
    /// The background's colours, in order (`0x00640c60`).
    static constexpr std::array<graphics::Rgba, 3> kColours{
        {{150, 50, 50, 255}, {50, 150, 50, 255}, {50, 50, 150, 255}}};
    /// The summary's place (`0x005100f0` / `0x005100f4`), the list's and the usage line's.
    static constexpr float kSummaryX = 0.01F;
    static constexpr float kSummaryY = 0.06F;
    static constexpr float kListX = 0.185F;
    static constexpr float kListY = 0.45F;
    static constexpr float kUsageX = 0.01F;
    static constexpr float kUsageY = 0.94F;
    /// The list's window (`0x00510108`) and clip height (`0x00510110`).
    static constexpr std::size_t kVisible = 5;
    static constexpr float kClipHeight = 0.5F;
    /// A header's size (`0x005100e4` / `0x005100e0`) and its circles' (`0x005100f8` / `0x005100fc`).
    static constexpr float kHeaderWidth = 0.35F;
    static constexpr float kHeaderHeight = 0.09F;
    static constexpr float kCircleWidth = 0.065F;
    static constexpr float kCircleHeight = 0.11F;
    /// How far a shown description reaches back into its header: 2 / the screen's height (448), in
    /// `ScrollingMenuItem_Update` and `_GetRect`.
    static constexpr float kRowOverlap = 2.0F / 448.0F;
    /// A description's delay after its entry takes the focus, and its reveal's step a frame (`0x005100d4`).
    static constexpr std::uint64_t kRevealDelayMs = 550;
    static constexpr float kRevealStep = 0.25F;
    /// The cue a description plays as it starts to open.
    static constexpr int kRevealCue = 0xd;

    /// The summary and the usage line placed and given their fonts.
    /// @coney-only Coney's construction (the original places them in GameStats_Init)
    GameStatsScreen();

    /// Shows strings from `strings` (which must outlive the screen; null shows empty texts).
    void setStrings(const GlobalStrings* strings) { m_strings = strings; }
    /// Sends front-end sound cues to `playCue` (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue) { m_playCue = std::move(playCue); }
    /// Draws with `batches` (not owned) from now on.
    void setBatches(const GameStatsBatches& batches) { m_batches = batches; }

    /// Opens the screen at `nowMs` over `data`: the summary, the list and the usage line.
    /// @orig 0x00215e78 GameStats_Init (GameStats.cpp)
    /// @orig 0x002155a8 GameStats_BuildMissionList (GameStats.cpp)
    void open(GameStatsData data, std::uint64_t nowMs);
    /// Releases the parts (mode 0x13's exit).
    /// @orig 0x002166d0 GameStats_Shutdown (GameStats.cpp)
    void close();

    /// The background's colour and the parts' frame; then the pad's command.
    /// @orig 0x00216a60 GameStats_Update (GameStats.cpp)
    /// @orig 0x002169e0 GameStats_UpdateMenu (GameStats.cpp)
    void update(const GuiFrame& frame) override;
    /// Background, summary, list and usage line.
    /// @orig 0x00216d48 GameStats_Render (GameStats.cpp)
    void render(const GuiCanvas& canvas) const override;

    /// Acts on one menu command at `nowMs`: back closes the screen, accept is taken and does nothing, up and down
    /// move through the list.
    /// @orig 0x002169a8 GameStats_OnCommand (GameStats.cpp)
    void handle(MenuCommand command, std::uint64_t nowMs);

    /// The background's colour `elapsedMs` after opening: red, green, blue and back, each a linear blend
    /// over kColourStepMs.
    /// @orig 0x00216a60 GameStats_Update (GameStats.cpp)
    [[nodiscard]] static graphics::Rgba backgroundColour(std::uint64_t elapsedMs);

    /// Whether the screen is open.
    [[nodiscard]] bool isOpen() const { return m_open; }
    /// Whether back closed it.
    [[nodiscard]] bool closed() const { return m_closed; }
    /// The data it shows.
    [[nodiscard]] const GameStatsData& data() const { return m_data; }
    /// The summary's text widget.
    [[nodiscard]] const TextWidget& summary() const { return m_summary; }
    /// The list's cursor.
    [[nodiscard]] const ScrollingMenu& list() const { return m_list; }
    /// Mission `index`'s header.
    [[nodiscard]] const CircledText& header(std::size_t index) const { return *m_headers.at(index); }
    /// Mission `index`'s sub-item.
    [[nodiscard]] const GameStatsSubItem& subItem(std::size_t index) const { return *m_subItems.at(index); }
    /// The background sprite.
    [[nodiscard]] const BaseWidget& background() const { return m_background; }

  private:
    // The batch a level's picture comes from.
    // @orig 0x00215490 GameStats_PickThumbBatch (GameStats.cpp)
    [[nodiscard]] graphics::SpriteBatch* thumbBatch(const GameStatsLevel& level) const;
    // Plays `cue`, if the screen has a way to.
    // @coney-only Coney's optional sound sink
    void playCue(int cue) const;

    const GlobalStrings* m_strings = nullptr;
    std::function<void(int cue)> m_playCue;
    GameStatsBatches m_batches;
    GameStatsData m_data;
    BaseWidget m_background{nullptr, kBackgroundRect};
    TextWidget m_summary;
    TextWidget m_usage;
    std::vector<std::unique_ptr<CircledText>> m_headers;
    std::vector<std::unique_ptr<GameStatsSubItem>> m_subItems;
    ScrollingMenu m_list;
    MenuInput m_input;
    std::size_t m_focused = 0;   // the entry whose description may open
    std::uint64_t m_focusMs = 0; // when it took the focus
    std::uint64_t m_openMs = 0;
    bool m_open = false;
    bool m_closed = false;
};

} // namespace coney::gui
