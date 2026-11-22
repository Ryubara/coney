// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "graphics/render_device.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

// The crew status list (HUD `+0x10`): a portrait with a pulsing status icon for each member of player 1's gang who is
// knocked out, cuffed or fighting far from the camera, and a caption naming the member when his entry appears.
// Research: docs/research/hud-functions-parts.md#crew-status

namespace coney::hud {

/// What puts a crew member on the list, in the order the original tests the three states.
enum class CrewState : std::uint8_t {
    None,        ///< None holds: not listed (an entry is hidden, then removed).
    KnockedOut,  ///< `Human_IsKnockedOut` (state `0x40000`): the red cross.
    Cuffed,      ///< `Human_IsCuffed` (state `0x20000`, the arrest): the blue badge.
    FightingFar, ///< Held as an enemy and attacked, more than 50 m from the camera.
};

/// One human as the list reads him each HUD step, by handle (the game's id for him, never a pointer): his gang and
/// brain type, his character type and name, and the facts the three states are tested on.
struct CrewMember {
    double handle = 0.0;         ///< The human's handle.
    int gang = -1;               ///< His brain's gang (`+0x20c`); -1 for none.
    int brainType = 0;           ///< His brain's type (`+0x04`, ai::BrainType's value: 0 the player, 3 a Warrior).
    int characterType = 0;       ///< His character type (`+0xcc`).
    std::string name;            ///< His name (`HuCreate`'s, or `HuSetName`'s).
    bool knockedOut = false;     ///< `Human_IsKnockedOut`.
    bool cuffed = false;         ///< `Human_IsCuffed`.
    bool hunted = false;         ///< Some brain holds him as an enemy (his brain's hostile count `+0x152` is not 0).
    bool attacked = false;       ///< An attacker holds one of his attack slots (`+0x1a4`).
    float cameraDistance = 0.0F; ///< His distance to the camera (`+0x334`), metres.
};

/// What the list is given each HUD step: the humans it may read, and each player slot's gang (-1 for a slot with no
/// player). Slot 0's gang is the one listed; the portraits tell the slots' gangs apart in co-op.
struct CrewFrame {
    std::span<const CrewMember> humans;
    std::array<int, kPlayers> playerGangs{-1, -1};
    bool coop = false; ///< A co-op game (game state `+0x158` not 0): the portraits show whose gang, not who.
};

/// The texts the caption is built from: `GSTRING.HUD[id]` and `CfgHUDColor`'s colour markup of slot `slot`. Either
/// may be empty, which reads as an empty string.
struct CrewTexts {
    std::function<std::string(std::uint32_t id)> hudString;
    std::function<std::string(int slot)> hudColour;
};

/// The `part_page0` status rectangles: knocked out, cuffed and fighting far off, and the one an entry starts with.
inline constexpr std::uint32_t kCrewKnockedOutRect = 0x1a;
inline constexpr std::uint32_t kCrewCuffedRect = 0x19;
inline constexpr std::uint32_t kCrewFightingRect = 0x15;
/// The `GSTRING.HUD` captions of the three states (225-227).
inline constexpr std::uint32_t kCrewKnockedOutString = 0xe1;
inline constexpr std::uint32_t kCrewCuffedString = 0xe2;
inline constexpr std::uint32_t kCrewFightingString = 0xe3;
/// The caption's colour tag (`HudColourTag_Get(5)`) and its display time, the fade included.
inline constexpr int kCrewCaptionColourSlot = 5;
inline constexpr std::uint32_t kCrewCaptionMs = 2500;
/// The fight alert's distance from the camera, metres: beyond it (its square above 2500).
inline constexpr float kCrewFightDistance = 50.0F;
/// The character types never listed: the bums (270-287).
inline constexpr int kCrewFirstBum = 270;
inline constexpr int kCrewLastBum = 287;
/// The status icon's pulse: a triangle wave of this period, ms.
inline constexpr std::uint64_t kCrewPulseMs = 800;

/// The state that lists `member` now, tested in the original's order: knocked out, then cuffed, then fighting far off
/// (hunted, attacked and more than 50 m from the camera).
/// @orig 0x001a0420 CrewStatus_Add (unknown)
/// @orig 0x001a0d28 CrewStatusEntry_Update (unknown)
[[nodiscard]] CrewState crewState(const CrewMember& member);

/// The portrait kind (0-11) of character type `type`: one of the Warriors' kinds, or nothing for any other type (the
/// original's kinds 12 and up, which no portrait has).
/// @orig 0x00229570 Human_GetPortraitKind (unknown)
[[nodiscard]] std::optional<int> crewPortraitKind(int type);

/// The `part_page0` portrait rectangle of `member`: outside co-op his portrait kind's; in co-op, or for a type with no
/// kind, `0x2c` in player slot 0's gang, `0x2d` in slot 1's and `0x28` in neither.
/// @orig 0x0019f988 CrewStatus_GetIcon (unknown)
[[nodiscard]] std::uint32_t crewPortraitRect(const CrewMember& member, const CrewFrame& frame);

/// The name a caption shows for `name`: its first letter upper-cased (any byte above `` ` `` moved down by 0x20, as
/// the original does), and `WarriorsSoldier` written `Warriors Soldier`.
/// @orig 0x001a0420 CrewStatus_Add (unknown)
[[nodiscard]] std::string crewCaptionName(std::string_view name);

/// The caption for `member` in `state`: `<DISPLAYTIME 2500><SIZE 1.0><CENTER>`, colour tag 5, his caption name, a
/// space and the state's `GSTRING.HUD` text. Empty for CrewState::None.
/// @orig 0x001a0420 CrewStatus_Add (unknown)
[[nodiscard]] std::string crewCaption(const CrewMember& member, CrewState state, const CrewTexts& texts);

/// The status icon's alpha share (0-1) at game time `nowMs`: 1 less the cube of an 800 ms triangle wave that is 1 at
/// the start and the end of each period and 0 in its middle.
/// @orig 0x001a0d28 CrewStatusEntry_Update (unknown)
[[nodiscard]] float crewStatusAlpha(std::uint64_t nowMs);

/// The default video mode's layout of the list: the grid's origin, the cell step (the grid's step times its
/// factors), the shift of a short row and the caption's offsets (`0x0050cfe4`-`0x0050d01c`, CrewStatus_Setup's
/// arguments from `HUD_LevelSetUp`).
struct CrewLayout {
    GuiPoint origin{0.407F, 0.07F};          ///< `0x0050d000`, `0x0050d004`.
    std::size_t perRow = 4;                  ///< CrewStatus_Setup's last argument.
    float columnStep = 0.12F * 0.6F;         ///< The step's x (0.12) times `0x0050cfec`.
    float rowStep = 0.08F * 1.2F;            ///< The step's y (0.08) times `0x0050cff0`.
    float shortRowShift = 0.11F;             ///< `0x0050d008`: added to x while fewer than four are listed ...
    float shortRowStep = -0.025F;            ///< ... with `0x0050d00c` per entry after the first.
    GuiPoint statusOffset{-0.069F, 0.0414F}; ///< The cell (0.03, -0.018) times `0x0050cfe4` (-2.3).
    float portraitSize = 0.05F * 1.8F;       ///< The icon size (0.05) times `0x0050cfe8`.
    float statusSize = 0.05F * 0.7F;         ///< The icon size times the aspect (0.7) and `0x0050cffc` (1.0).
    float captionX = -0.05F + 0.13F;         ///< `0x0050cff4` plus `0x0050d010`, from the origin's x ...
    float captionStep = 0.01F;  ///< ... with `0x0050d014` per entry (count mod 4, less one; 4 and 8 count 1).
    float captionDrop = -0.01F; ///< `0x0050cff8`, added under the last row.
};

/// The list's layout in the default video mode (the 16:9, PAL and progressive values are not ported, #890).
inline constexpr CrewLayout kCrewLayout{};
/// The portrait's colour.
inline constexpr graphics::Rgba kCrewPortraitColour{191, 191, 191, 255};
/// The caption widget's colour (its colour tag replaces it for the text after the tag).
inline constexpr graphics::Rgba kCrewCaptionColour{128, 128, 128, 255};

/// The crew status list: up to eight entries, each a crew member's handle and his place in the order the entries were
/// added, and one caption for the whole list. add() runs every update for each member of player 1's gang; update()
/// re-tests each listed member, places the live entries in a grid of four a row, hides an entry whose state cleared
/// (removed at the next update) and removes one whose human is gone or hidden; render() draws the entries and the
/// caption.
///
/// Plain data held by handle: a copy of a list is the same list.
///
/// Research: docs/research/hud-functions-parts.md#crew-status
/// @orig 0x0019faf0 CrewStatus_Init (unknown)
class CrewStatusList {
  public:
    /// The most entries the list holds.
    static constexpr std::size_t kEntries = 8;

    /// One entry slot.
    struct Entry {
        std::optional<std::uint32_t> order;     ///< Its place in the list (`+0x44` + slot); nothing for a free slot.
        double handle = 0.0;                    ///< The member's handle (entry `+0x240`).
        bool shown = false;                     ///< Not hidden: the last update found a state.
        std::uint32_t portrait = 0;             ///< The portrait rectangle (crewPortraitRect()).
        std::uint32_t status = kCrewCuffedRect; ///< The status rectangle.
        GuiPoint place;                         ///< The portrait's centre, as the last update placed it.

        friend bool operator==(const Entry&, const Entry&) = default;
    };

    /// Lists `member` when a state holds for him, he is not a bum (types 270-287), and he is in player slot 0's gang
    /// or has a player's or a Warrior's brain; one already listed is not added twice, and a ninth is dropped. A new
    /// entry takes the first free slot and the next order, and the caption becomes his (crewCaption()), shown from
    /// `nowMs`. Returns whether he is listed now.
    /// @orig 0x001a0420 CrewStatus_Add (unknown)
    /// @orig 0x001b3960 HUD_TagAddHuman (unknown)
    bool add(const CrewMember& member, const CrewFrame& frame, const CrewTexts& texts, std::uint64_t nowMs);

    /// Drops `handle`'s entry and moves every later one up a place. Returns whether he was listed.
    /// @orig 0x001a07f0 CrewStatus_Remove (unknown)
    bool remove(double handle);

    /// One update: each listed slot in turn, an entry that is hidden or whose human is not in `frame` is removed;
    /// any other is placed at its cell and re-tested (updateEntry()). Then the caption is placed under the last row.
    /// @orig 0x0019fda0 CrewStatus_Update (unknown)
    void update(const CrewFrame& frame);

    /// Adds the shown entries' portraits and status icons to `canvas.parts`, the icons pulsing at `nowMs`, then the
    /// caption while its 2.5 s last.
    /// @orig 0x001a03a8 CrewStatus_Render (unknown)
    /// @orig 0x001a0c90 CrewStatusEntry_Render (unknown)
    void render(const HudCanvas& canvas, std::uint64_t nowMs) const;

    /// How many are listed (`+0x40`).
    [[nodiscard]] std::size_t count() const { return m_count; }
    /// The slots.
    [[nodiscard]] const std::array<Entry, kEntries>& entries() const { return m_entries; }
    /// The listed entry of `handle`; null when he is not listed.
    /// @coney-only a lookup for the HUD's callers and tests; the original walks its slots where it needs one
    [[nodiscard]] const Entry* find(double handle) const;
    /// The caption's marked-up text (`+0x1328`).
    [[nodiscard]] const std::string& caption() const { return m_caption; }
    /// When the caption was set (game time, ms).
    [[nodiscard]] std::uint64_t captionSinceMs() const { return m_captionSinceMs; }
    /// Where the last update placed the caption.
    [[nodiscard]] GuiPoint captionPlace() const { return m_captionPlace; }

    /// The portrait's centre of the entry at `order` while `count` are listed.
    /// @orig 0x0019fda0 CrewStatus_Update (unknown)
    [[nodiscard]] static GuiPoint entryPlace(std::uint32_t order, std::size_t count);
    /// The caption's place after an update placed `placed` entries with `count` listed.
    /// @orig 0x0019fda0 CrewStatus_Update (unknown)
    [[nodiscard]] static GuiPoint captionPlaceFor(std::size_t placed, std::size_t count);

    friend bool operator==(const CrewStatusList&, const CrewStatusList&) = default;

  private:
    // One entry's update: hidden unless its member is in slot 0's gang and a state holds; else the state's status
    // rectangle.
    // @orig 0x001a0d28 CrewStatusEntry_Update (unknown)
    static void updateEntry(Entry& entry, const CrewMember& member, const CrewFrame& frame);

    std::array<Entry, kEntries> m_entries{};
    std::size_t m_count = 0;
    std::string m_caption;
    std::uint64_t m_captionSinceMs = 0;
    GuiPoint m_captionPlace;
};

} // namespace coney::hud
