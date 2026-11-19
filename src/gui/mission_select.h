// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/render_device.h"
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

/// A point on the mission select's map, in GUI coordinates.
struct MapPoint {
    float x = 0.0F; ///< GUI x.
    float y = 0.0F; ///< GUI y.

    friend bool operator==(const MapPoint&, const MapPoint&) = default;
};

/// Which column of the map tables a video mode reads (`LevelRecord_GetMapPos`): 0 the default, 1 16:9 (flag `0x04`),
/// 2 progressive (flag `0x20`), 3 flag `0x02`, 4 flag `0x02` with 16:9.
/// @orig 0x0041f540 LevelRecord_GetMapPos (unknown)
[[nodiscard]] std::size_t mapColumn(bool flag02, bool widescreen, bool flag20);

/// The text after the first `:` of a level record's second name (`<label>:<area>`): the mission's area, which names
/// the area's title and whose CRC-32 groups the missions; nothing when the name has no `:`.
/// @orig 0x0041f3e8 LevelRecord_GetAreaName (unknown)
[[nodiscard]] std::optional<std::string_view> missionAreaName(const LevelRecord& record);

/// The text before the first `:` of a level record's second name, at most three characters: the mission's number,
/// circled on its entry; empty when the name starts with `:` or is empty.
/// @orig 0x0041f428 LevelRecord_GetLabel (unknown)
[[nodiscard]] std::string missionLabel(const LevelRecord& record);

/// A label read as a base-10 number as `strtol` reads it (leading spaces, a sign, the digits; 0 when none).
/// @orig 0x00433a68 Str_ToInt (unknown)
[[nodiscard]] int labelNumber(std::string_view label);

/// A mission's marker on the map: the record's mission byte (`+0x0c`, 1-23) row of the table at `0x005148e0`, column
/// `column` (mapColumn()). A record without a mission byte, or past the table, gets (0, 0) (Coney's stand-in: the
/// original returns its static result unchanged, never seen for a listed mission).
/// @orig 0x0041f540 LevelRecord_GetMapPos (unknown)
[[nodiscard]] MapPoint missionMapPoint(const LevelRecord& record, std::size_t column);

/// Where an area's gang logo sits over the map: the same row and column of the table at `0x00514c78`.
/// @orig 0x0041f5f8 LevelRecord_GetMapPos2 (unknown)
[[nodiscard]] MapPoint missionLogoPoint(const LevelRecord& record, std::size_t column);

/// The halo's colour for a level of area byte `area` (record `+0x7c`): 1 (186, 139, 53), 2 (48, 137, 81),
/// 3 (64, 95, 142), 4 (76, 53, 44). For any other value the original leaves its output unwritten; Coney gives black.
/// @orig 0x0041f340 LevelRecord_GetAreaColour2 (unknown)
[[nodiscard]] graphics::Rgba areaHaloColour(double area);

/// Whether the mission select lists level record `record`: its number is 2-99 but not 95 (the hub) nor 60-65 (the
/// Armies of the Night), and the level is complete.
/// @orig 0x001bc100 MissionSelect_IsMissionListed (MissionSelectHUD.cpp)
[[nodiscard]] bool isMissionSelectListed(const LevelRecord& record, const UnlockRecords& unlocks,
                                         const SavedProgress& progress);

/// One mission of the mission select (an `MS_Mission`): its record and the entry and tile it shows.
struct MissionSelectMission {
    std::size_t record = 0;        ///< The level record's index (`+0x00`).
    int level = 0;                 ///< The level number (record `+0x04`): what onChoose receives.
    std::size_t checkpoints = 0;   ///< The record's section count (`+0x08`): how many letters it offers.
    std::string name;              ///< The entry's text: the level's menu name (record `+0x49`).
    std::string label;             ///< The circled characters: missionLabel().
    graphics::Rgba circleColour{}; ///< The level's colour, purple when the label reads as 0.
    std::size_t letter = 0;        ///< The chosen checkpoint, from 0 (`'A'`; the tile's `+0x60`).
    bool open = false;             ///< The tile is highlighted: the mission is open.
};

/// One area of the mission select (an `MS_Area`): the missions whose area names hash alike.
struct MissionSelectArea {
    std::size_t firstRecord = 0;                ///< The first mission's record (`+0x00`): the marker's and logo's.
    std::uint32_t hash = 0;                     ///< The CRC-32 of the area's name (`+0x04`).
    std::string name;                           ///< The area's name: the title shows it.
    MapPoint marker;                            ///< The marker's place (the first mission's map point).
    MapPoint logo;                              ///< The gang logo's place (the first mission's logo point).
    std::uint32_t logoWord = 0;                 ///< The logo's sprite word (record `+0x78`).
    graphics::Rgba halo{};                      ///< The halo's colour (areaHaloColour of the first mission).
    std::vector<MissionSelectMission> missions; ///< The missions (`+0x0c`), sorted by their label.
    std::size_t selected = 0;                   ///< The list's selected mission (the area's menu, `+0x20`).
};

/// The areas the mission select lists, as `MissionSelect_BuildAreas` makes them: the level records in index order,
/// each one isMissionSelectListed() passes going into the area of its name's CRC-32 (a new area made for a new hash);
/// the areas then sorted by their marker's y, the larger first, and each area's missions by labelNumber(), the smaller
/// first. The original's sorts are `std::sort`, which keeps the order of equal keys for 16 entries or fewer; so does
/// Coney's (a stable sort).
/// @orig 0x001bcc48 MissionSelect_BuildAreas (MissionSelectHUD.cpp)
/// @orig 0x001bca70 MissionSelect_AddArea (MissionSelectHUD.cpp)
/// @orig 0x001bc930 MissionSelect_AddMission (MissionSelectHUD.cpp)
/// @orig 0x001bca30 MissionSelect_FindArea (MissionSelectHUD.cpp)
[[nodiscard]] std::vector<MissionSelectArea> buildMissionSelectAreas(const LevelTable& levels,
                                                                     const UnlockRecords& unlocks,
                                                                     const SavedProgress& progress, std::size_t column);

/// The mission select's layout for one video mode: what `MissionSelect_Init` writes into `0x0050e850`-`0x0050e894`.
///
/// Research: docs/research/frontend.md#fn-missionselecthud
struct MissionSelectLayout {
    float x = 0.8F;                 ///< `0x0050e850`: the title's and the list's x.
    float y = 0.083F;               ///< `0x0050e854`: the title's y.
    float textX = 0.44F;            ///< `0x0050e858`: the text's offset (the title's width).
    float textY = 0.074F;           ///< `0x0050e85c`: the title's height; the list starts this far below it.
    float listGap = 0.01F;          ///< `0x0050e860`: the list's item gap and its extra drop.
    float circleOffset = 0.008F;    ///< `0x0050e864`: where the circles start, from the left edge.
    float tile = 0.35F;             ///< `0x0050e86c`: a mission tile's height.
    float circleW = 0.045F;         ///< `0x0050e870`.
    float circleH = 0.07F;          ///< `0x0050e874`.
    float background = 1.12F;       ///< `0x0050e878`: the map's width.
    float backgroundHeight = 1.05F; ///< `0x0050e87c`: the map's height.
    float logo = 0.8F;              ///< `0x0050e880`: the logo's size.
    float logoWidth = 1.4286F;      ///< `0x0050e884`: the logo's width factor.
    float marker = 0.0147F;         ///< `0x0050e888`: a marker's size (the halo's base).
    float usageX = 0.0F;            ///< `0x0050e88c`.
    float usageY = 0.81F;           ///< `0x0050e890`.
    bool alternative = false;       ///< `0x0050e894`: the sheets of record 21 instead of 20.
    std::size_t column = 0;         ///< The map tables' column (mapColumn()).
    float stepDivisor = CircledText::kStepDivisor; ///< The circles' step divisor: 2 with flag `0x02`.

    /// The layout of the mode the device's flags give: `flag02` (with or without `widescreen`), then `flag20`, then
    /// `widescreen`, else the default; the progressive mode keeps the static values, the default mode's.
    /// @orig 0x001bcdd8 MissionSelect_Init (MissionSelectHUD.cpp)
    [[nodiscard]] static MissionSelectLayout forFlags(bool flag02, bool widescreen, bool flag20);
};

/// The sprite batches the screen draws with; any may be null (that part is not drawn).
struct MissionSelectBatches {
    graphics::SpriteBatch* frame = nullptr; ///< Record 20 or 21 (`+0x90`): the map, the markers, the halo, the texts.
    graphics::SpriteBatch* logos = nullptr; ///< Record 25 (`gang_logos`, `+0x94`).
};

/// The mission select screen (`GUI/MissionSelectHUD.cpp`, game mode 0x10's widget): the map with a marker per area,
/// the chosen area's gang logo and pulsing marker with its halo, the area's title and its list of missions, and the
/// usage line.
///
/// - **The area menu** (`MissionSelect_OnMenuCommand`): up shows the next area and down the previous one (cue 4; at
///   either end cue `0xe` and the refused flag, which turns the d-pad's repeat off until the area changes); left and
///   right are not taken; accept moves the focus to the area's list (cue 8); back cancels the screen, without a cue.
/// - **The list** (`MissionSelect_OnListCommand`): up and down move between the missions (the list's own moves) until
///   accept opens the selected one; then up and down step its checkpoint letter down and up within its section count
///   (cue 4, or `0xe` and the refused flag at either end), a second accept starts it (cue 8), and back closes it, or,
///   with none open, gives the focus back to the area menu (cue `0xf` either way).
/// - **The start** (`MissionSelect_UpdateWidget`): once the clock is more than kStartDelayMs past the start stamp, the
///   choice is made (`MissionSelect_OnAccept`): cue 8, the mission's level and its letter + 1 as the checkpoint, and
///   the chosen flag. The start itself stamps no time: only `MissionSelect_Init` writes the stamp, a 0, so in play (the
///   clock long past 2.3 s) the choice comes on the next frame. The original's clock is the real-time clock since the
///   machine started; Coney's is the game time the screen is given (Coney's stand-in).
/// - **The marker** of the area shown pulses between 1 and 2 times kMarker in size, one way each kPulseMs, with its
/// halo
///   1.55 times as big in the area's halo colour × the map's grey.
///
/// Coney's stand-in: the mission tile (`MS_Mission`, the selected entry's description: its picture, the best score and
/// unlockables lines and its reveal) is drawn only as its letter (#1451).
///
/// Research: docs/research/frontend.md#fn-missionselecthud
class MissionSelectScreen : public Widget {
  public:
    /// How far past the start stamp the clock must be for the choice, in milliseconds (`0x8fc`: more than this).
    static constexpr std::uint64_t kStartDelayMs = 2300;
    /// The marker's pulse: one way, in milliseconds (`+0x698`).
    static constexpr std::uint64_t kPulseMs = 400;
    /// The halo's size against the marker's.
    static constexpr float kHaloScale = 1.55F;
    /// The cues.
    static constexpr int kMoveCue = 4;
    static constexpr int kRefusedCue = 0xe;
    static constexpr int kAcceptCue = 8;
    static constexpr int kBackCue = 0xf;
    /// The usage line's string.
    static constexpr std::uint32_t kUsageString = 0x20;
    /// The frame sheets' records: the default and the alternative (16:9 or progressive).
    static constexpr std::uint32_t kFrameRecord = 20;
    static constexpr std::uint32_t kAlternativeFrameRecord = 21;
    /// The gang logos' record.
    static constexpr std::uint32_t kLogoRecord = 25;
    /// The rectangles of the frame sheet: the map, the title's back, the marker; the halo (2, 3 in the alternative).
    static constexpr std::size_t kMapRect = 0;
    static constexpr std::size_t kMarkerRect = 5;
    /// The map's grey, which also tints the halo.
    static constexpr graphics::Rgba kMapGrey{144, 144, 144, 255};
    /// The entries' colour when the label reads as 0.
    static constexpr graphics::Rgba kPurple{106, 65, 131, 255};
    /// The title's circles' size and the two font sizes.
    static constexpr float kTitleCircle = 0.055F;
    static constexpr float kTitleSize = 1.25F;
    static constexpr float kEntrySize = 0.92F;

    /// Shows strings from `strings` (which must outlive the screen; null shows empty texts).
    void setStrings(const GlobalStrings* strings) { m_strings = strings; }
    /// Sends front-end sound cues to `playCue` (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue) { m_playCue = std::move(playCue); }
    /// Draws with `batches` (not owned) from now on.
    void setBatches(const MissionSelectBatches& batches) { m_batches = batches; }
    /// Lays the screen out with `layout` from the next open().
    void setLayout(const MissionSelectLayout& layout) { m_layout = layout; }
    /// The layout.
    [[nodiscard]] const MissionSelectLayout& layout() const { return m_layout; }

    /// Opens the screen at `nowMs` over `areas`: the first area shown, the area menu focused, nothing chosen.
    /// @orig 0x001bcdd8 MissionSelect_Init (MissionSelectHUD.cpp)
    /// @orig 0x001bc5a8 MissionSelect_Build (MissionSelectHUD.cpp)
    void open(std::vector<MissionSelectArea> areas, std::uint64_t nowMs);
    /// Releases the parts.
    /// @orig 0x001bd428 MissionSelect_Shutdown (MissionSelectHUD.cpp)
    void close();

    /// Acts on one menu command at `nowMs`: the area menu's when it has the focus, else the list's.
    /// @orig 0x001be130 MissionSelect_OnCommand (MissionSelectHUD.cpp)
    void handle(MenuCommand command, std::uint64_t nowMs);
    /// The frame: until a choice or a cancel, the pad's command, the title of a newly shown area, the markers, the
    /// pulse and the halo; while a start waits, the choice once the clock is past kStartDelayMs.
    /// @orig 0x001be198 MissionSelect_UpdateWidget (MissionSelectHUD.cpp)
    void update(const GuiFrame& frame) override;
    /// The map, the logo, the halo, every marker, the title, the area's list and the usage line.
    /// @orig 0x001be848 MissionSelect_Render (MissionSelectHUD.cpp)
    void render(const GuiCanvas& canvas) const override;

    /// Whether the screen is open.
    [[nodiscard]] bool isOpen() const { return m_open; }
    /// The areas.
    [[nodiscard]] const std::vector<MissionSelectArea>& areas() const { return m_areas; }
    /// The area at the cursor (`+0x584`).
    [[nodiscard]] std::size_t cursor() const { return m_cursor; }
    /// The area shown (`+0x580`); none before the first update.
    [[nodiscard]] std::optional<std::size_t> shownArea() const { return m_shown; }
    /// Whether the area menu has the focus (else the list has it).
    [[nodiscard]] bool menuFocused() const { return m_menuFocused; }
    /// Whether a mission is open (`+0x6a4`).
    [[nodiscard]] bool missionOpen() const { return m_missionOpen; }
    /// The refused flag (`+0x6a8`): the d-pad does not repeat.
    [[nodiscard]] bool refused() const { return m_refused; }
    /// The selected mission of the area at the cursor.
    [[nodiscard]] std::size_t selectedMission() const;
    /// Whether a start is waiting for its delay (`+0x69c` = 1).
    [[nodiscard]] bool starting() const { return m_start == Start::Waiting; }
    /// Whether back cancelled the screen.
    [[nodiscard]] bool cancelled() const { return m_cancelled; }
    /// Whether a mission was chosen.
    [[nodiscard]] bool chosen() const { return m_chosen; }
    /// Cancelled or chosen (`MissionSelect_IsDone`).
    [[nodiscard]] bool done() const { return m_cancelled || m_chosen; }
    /// The chosen mission's level number.
    [[nodiscard]] int chosenLevel() const { return m_chosenLevel; }
    /// The chosen checkpoint: the letter + 1.
    [[nodiscard]] int chosenCheckpoint() const { return m_chosenCheckpoint; }
    /// The shown marker's size factor now: 1-2 (the pulse).
    [[nodiscard]] float pulse() const { return m_pulse; }
    /// The title.
    [[nodiscard]] const CircledText& title() const { return m_title; }
    /// The entry of mission `index` of the area at the cursor.
    [[nodiscard]] const CircledText& entry(std::size_t index) const { return *m_entries.at(index); }
    /// The marker of area `index`.
    [[nodiscard]] const BaseWidget& marker(std::size_t index) const { return *m_markers.at(index); }
    /// The halo.
    [[nodiscard]] const BaseWidget& halo() const { return m_halo; }
    /// The gang logo.
    [[nodiscard]] const BaseWidget& logo() const { return m_logo; }

  private:
    // The start's state (`+0x69c`): none, waiting for the delay, accepted.
    enum class Start : std::uint8_t { None, Waiting, Accepted };

    // Commands while the area menu has the focus.
    // @orig 0x001be070 MissionSelect_OnMenuCommand (MissionSelectHUD.cpp)
    void onMenuCommand(MenuCommand command, std::uint64_t nowMs);
    // Commands while the list has the focus; returns whether it took the command.
    // @orig 0x001bde18 MissionSelect_OnListCommand (MissionSelectHUD.cpp)
    bool onListCommand(MenuCommand command, std::uint64_t nowMs);
    // The cursor one area on, or the refusal at the last.
    // @orig 0x001bdc48 MissionSelect_NextArea (MissionSelectHUD.cpp)
    void nextArea();
    // The cursor one area back, or the refusal at the first.
    // @orig 0x001bdcd0 MissionSelect_PrevArea (MissionSelectHUD.cpp)
    void prevArea();
    // Clears the refusal and fills the area at the cursor.
    // @orig 0x001bdc10 MissionSelect_ShowArea (MissionSelectHUD.cpp)
    void showArea();
    // Makes the area's list: its menu and an entry per mission.
    // @orig 0x001bd838 MissionSelect_FillArea (MissionSelectHUD.cpp)
    void fillArea();
    // The choice: cue 8, the level, the checkpoint and the chosen flag.
    // @orig 0x001bdd38 MissionSelect_OnAccept (MissionSelectHUD.cpp)
    void onAccept();
    // The mission selected in the area at the cursor.
    // @coney-only Coney's accessor over the area's menu
    MissionSelectMission& current();
    // Plays `cue`, if the screen has a way to.
    // @coney-only Coney's optional sound sink
    void playCue(int cue) const;

    const GlobalStrings* m_strings = nullptr;
    std::function<void(int cue)> m_playCue;
    MissionSelectBatches m_batches;
    MissionSelectLayout m_layout;
    std::vector<MissionSelectArea> m_areas;
    std::vector<std::unique_ptr<BaseWidget>> m_markers;  // widgets are not movable
    std::vector<std::unique_ptr<CircledText>> m_entries; // the entries of the area at the cursor
    BaseWidget m_map{nullptr, kMapRect};
    BaseWidget m_logo{nullptr, 0};
    BaseWidget m_halo{nullptr, 2};
    CircledText m_title;
    TextWidget m_usage;
    TextWidget m_letter; // the open mission's letter: the tile's stand-in (#1451)
    ScrollingMenu m_list;
    MenuInput m_input;
    std::size_t m_cursor = 0;           // `+0x584`
    std::optional<std::size_t> m_shown; // `+0x580`
    bool m_menuFocused = true;
    bool m_missionOpen = false;       // `+0x6a4`
    bool m_refused = false;           // `+0x6a8`
    Start m_start = Start::None;      // `+0x69c`
    std::uint64_t m_startMs = 0;      // `+0x6a0`: the start stamp, only open() writes it (0)
    bool m_pulseUp = false;           // `+0x690`: 0 shrinks from 2 to 1, 1 grows from 1 to 2
    std::uint64_t m_pulseStartMs = 0; // `+0x694`
    float m_pulse = 1.0F;
    bool m_cancelled = false;
    bool m_chosen = false;
    int m_chosenLevel = 0;
    int m_chosenCheckpoint = 1;
    bool m_open = false;
};

} // namespace coney::gui
