// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gui/base_widget.h"
#include "gui/profile_management_gui/pm_list_screen.h"
#include "gui/profile_management_gui/pm_screen.h"
#include "gui/text_widget.h"

namespace coney::gui {

/// PM_Create's on-screen keyboard as logic: the characters of global string `0x97`, then OK and DEL, in rows of 12, 12,
/// 12 and 11 cells. A blank cell (a space in the string) is not selectable. Up from cell 8 and down from cell 32 jump
/// to OK.
///
/// Coney's choices where the page is silent: left and right move within the row without wrapping, skipping blank
/// cells; up and down keep the column (clamped to the row, then the nearest selectable cell to its left); a string
/// whose cells do not fill the four rows exactly is laid out in rows of 12.
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x001cc1a0 NameKeyboard::NameKeyboard (unknown)
class NameKeyboard {
  public:
    /// What a cell does.
    enum class Key : std::uint8_t { Character, Ok, Delete };
    /// One cell: its label and what it does.
    struct Cell {
        std::string label;
        Key key = Key::Character;
        bool selectable = true;
    };
    /// The row sizes of the original's keyboard.
    static constexpr std::array<std::size_t, 4> kRows{12, 12, 12, 11};
    /// The cells whose up (8) or down (32) jumps to OK.
    static constexpr std::size_t kUpToOk = 8;
    static constexpr std::size_t kDownToOk = 32;

    /// Builds the cells from `characters` (`0x97`) and the labels of OK (`0x99`) and DEL (`0x9a`); selects the first.
    void set(std::string_view characters, std::string_view ok, std::string_view del);
    /// Moves the cursor: true when it moved.
    bool move(MenuCommand command);
    /// Puts the cursor on OK.
    void selectOk() { m_selected = okIndex(); }

    [[nodiscard]] const std::vector<Cell>& cells() const { return m_cells; }
    [[nodiscard]] const std::vector<std::size_t>& rows() const { return m_rows; }
    [[nodiscard]] std::size_t selected() const { return m_selected; }
    [[nodiscard]] std::size_t okIndex() const { return m_cells.empty() ? 0 : m_cells.size() - 2; }
    /// The row and column of cell `index`.
    [[nodiscard]] std::pair<std::size_t, std::size_t> cell(std::size_t index) const;

  private:
    // The selectable cell at `row`, `column` or the nearest one to its left; nothing when the row has none.
    [[nodiscard]] std::optional<std::size_t> nearest(std::size_t row, std::size_t column) const;
    // The first index of `row`.
    [[nodiscard]] std::size_t rowStart(std::size_t row) const;

    std::vector<Cell> m_cells;
    std::vector<std::size_t> m_rows;
    std::size_t m_selected = 0;
};

/// PM_Create, enter a name: title `0x89` (y 0.5), the name being made (size 2.0, grey, y 0.565) and the keyboard (size
/// 1.0, red, `big_font`, rows from y 0.625). A character adds itself (`_` is a space) up to 8, cue `0xa`, or `0xe` when
/// the name is full; reaching 8 puts the cursor on OK. DEL removes one (`0xc`). OK: an empty or all-space name plays
/// `0xe`; otherwise `0xb`, then a name another slot already uses shows `0x86`, else the result is 0 (PM_Difficulty).
/// Back empties the name and pops. Entering keeps the free slot as the chosen one. Keyboard moves play cue 7.
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x00204f08 PM_Create::PM_Create (PM_Create.cpp)
class PmCreate final : public PmScreen {
  public:
    static constexpr std::uint32_t kTitleString = 0x89;
    static constexpr std::uint32_t kCharactersString = 0x97;
    static constexpr std::uint32_t kOkString = 0x99;
    static constexpr std::uint32_t kDelString = 0x9a;
    static constexpr std::uint32_t kNameUsedString = 0x86;
    static constexpr float kTitleY = 0.5F;
    static constexpr float kNameY = 0.565F;
    static constexpr float kKeysY = 0.627F;
    static constexpr float kKeyRowPitch = 0.0567F;    ///< Measured: rows at 0.627, 0.684, 0.740, 0.797.
    static constexpr float kKeyColumnPitch = 0.0363F; ///< Measured: centres 0.014 to 0.413 over eleven steps.
    static constexpr float kErrorY = 0.45F;

    explicit PmCreate(PmShared& shared) : PmScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Create"; }

    /// @orig 0x002056d8 PM_Create::HandleCommand (PM_Create.cpp)
    /// @orig 0x001cca80 NameKeyboard::HandleCommand (unknown)
    int handle(MenuCommand command) override;

    [[nodiscard]] const NameKeyboard& keyboard() const { return m_keyboard; }
    /// Whether the "name already used" message shows.
    [[nodiscard]] bool nameUsedShown() const { return m_nameUsed; }

  protected:
    /// @orig 0x002050f0 PM_Create::Init (PM_Create.cpp)
    void open() override;
    void close() override;
    void draw() override;

  private:
    // Accept on the cursor's cell.
    int press();

    NameKeyboard m_keyboard;
    bool m_nameUsed = false;
    bool m_keysPlaced = false; // DEL placed after OK (needs the fonts, so on the first draw)
    TextWidget m_title;
    TextWidget m_name;
    TextWidget m_error;
    TextWidget m_usage;
    std::vector<std::unique_ptr<TextWidget>> m_keys; // pointers: a TextWidget cannot move
};

/// PM_Difficulty: title `0x8f` and one item a row, `0x90`, `0x91`, `0x92` and `0x93` when the fourth is unlocked;
/// index 1 selected (3 with four items). Accept keeps the index as the profile's difficulty (`W_GameState + 0x43c`) and
/// returns 0 (PM_Light).
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x002065f0 PM_Difficulty::PM_Difficulty (PM_Difficulty.cpp)
class PmDifficulty final : public PmListScreen {
  public:
    static constexpr std::uint32_t kTitleString = 0x8f;
    static constexpr std::uint32_t kFirstString = 0x90;

    explicit PmDifficulty(PmShared& shared) : PmListScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Difficulty"; }

  protected:
    /// @orig 0x002067d8 PM_Difficulty::Init (PM_Difficulty.cpp)
    void setUp() override;
    /// @orig 0x00206d88 PM_Difficulty::HandleCommand (PM_Difficulty.cpp)
    int accept(int code) override;
};

/// PM_Light, the brightness: a square of `menu_system` rectangle 5 drawn in (v, v, v), a bar filled red to v / 100 and
/// the hint `0x118`. v starts at 40; left and right step it by 5 within 0-100 with cue 6, and each change sets the
/// game's brightness (`Gamma_Set`, `W_GameState + 0x57a4`). Accept marks a profile in use (save-system `+0x124`) and
/// returns 0 (PM_Subtitles).
///
/// Coney's choices: the steps come from the menu commands' left and right (the auto-repeating d-pad, or the stick past
/// half way); a step past either end plays nothing; the brightness is only stored (no colour is applied yet, its
/// target is an open question on the page).
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x00208320 PM_Light::PM_Light (PM_Light.cpp)
class PmLight final : public PmScreen {
  public:
    static constexpr int kStart = 40;
    static constexpr int kStep = 5;
    static constexpr int kMax = 100;
    static constexpr std::uint32_t kHintString = 0x118;
    static constexpr std::size_t kSquareRect = 5;
    static constexpr std::size_t kBarRect = 1;
    static constexpr float kSquareY = 0.71F;
    static constexpr float kSquareHeight = 0.14F;
    static constexpr float kBarY = 0.815F;
    static constexpr float kBarWidth = 0.45F; ///< 0.6 overlay units on the 4:3 screen.
    static constexpr float kBarHeight = 0.025F;
    static constexpr float kHintX = 0.13F;
    static constexpr float kHintY = 0.654F;

    explicit PmLight(PmShared& shared) : PmScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Light"; }

    /// @orig 0x00208cc0 PM_Light::HandleCommand (PM_Light.cpp)
    /// @orig 0x00208c18 PM_Light::SetValue (PM_Light.cpp)
    int handle(MenuCommand command) override;

    /// The brightness shown, 0-100.
    [[nodiscard]] int value() const { return m_value; }

  protected:
    /// @orig 0x00208510 PM_Light::Init (PM_Light.cpp)
    void open() override;
    void close() override;
    void draw() override;

  private:
    int m_value = kStart;
    BaseWidget m_square{nullptr, kSquareRect};
    BaseWidget m_barBack{nullptr, kBarRect};
    BaseWidget m_barFill{nullptr, kBarRect};
    TextWidget m_hint;
    TextWidget m_usage;
};

/// PM_Subtitles: title `0x94` and a one-row grid of `0x95` ON and `0x96` OFF, OFF selected for English and ON for the
/// other languages. Accept keeps the choice (`W_GameState + 0x438`), plays cue 9 and ends the menus: a new game was
/// started, the profile is created on the way out, and the story starts.
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x0020c8d0 PM_Subtitles::PM_Subtitles (PM_Subtitles.cpp)
class PmSubtitles final : public PmListScreen {
  public:
    static constexpr std::uint32_t kTitleString = 0x94;
    static constexpr std::uint32_t kOnString = 0x95;
    static constexpr std::uint32_t kOffString = 0x96;

    explicit PmSubtitles(PmShared& shared) : PmListScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Subtitles"; }

  protected:
    /// @orig 0x0020cab8 PM_Subtitles::Init (PM_Subtitles.cpp)
    void setUp() override;
    /// @orig 0x0020cf30 PM_Subtitles::HandleCommand (PM_Subtitles.cpp)
    int accept(int code) override;
};

} // namespace coney::gui
