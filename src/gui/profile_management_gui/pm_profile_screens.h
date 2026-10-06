// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "gui/profile_management_gui/pm_list_screen.h"
#include "gui/text_widget.h"

namespace coney::gui {

/// PM_NumPlayers, how many play: a one-row grid of `0x8d` (1 player) and `0x8e` (2 players) at (x, 0.81). Accept on 1
/// player clears the two-player flag and returns 0 (PM_Profile). The first accept on 2 players shows the prompt `0x77`
/// (player 2, press START; size 1.15, red, y 0.74), blinking 0 → 255 → 0 in 1,500 ms halves, and waits for START on
/// the second pad, which accepts again: the flag is set and the result is 0. Moving back to 1 player hides the prompt.
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x0020b020 PM_NumPlayers::PM_NumPlayers (PM_NumPlayers.cpp)
class PmNumPlayers final : public PmListScreen {
  public:
    /// The items' strings and the prompt's.
    static constexpr std::uint32_t kOnePlayerString = 0x8d;
    static constexpr std::uint32_t kTwoPlayersString = 0x8e;
    static constexpr std::uint32_t kPromptString = 0x77;
    /// The prompt's centre line.
    static constexpr float kPromptY = 0.74F;

    /// A screen over `shared`, which must outlive it.
    explicit PmNumPlayers(PmShared& shared) : PmListScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_NumPlayers"; }

    /// Whether the screen waits for player 2's START.
    [[nodiscard]] bool waitingForPlayerTwo() const { return m_waiting; }
    /// The prompt, for tests.
    [[nodiscard]] const TextWidget& prompt() const { return m_prompt; }

  protected:
    /// @orig 0x0020b208 PM_NumPlayers::Init (PM_NumPlayers.cpp)
    void setUp() override;
    /// @orig 0x0020b710 PM_NumPlayers::HandleCommand (PM_NumPlayers.cpp)
    int accept(int code) override;
    // A move back to 1 player hides the prompt.
    int direction(MenuCommand command) override;
    // START on the second pad while the prompt shows accepts 2 players.
    int poll() override;
    void drawExtras() override;
    void closeExtras() override { m_prompt.shutdown(); }

  private:
    TextWidget m_prompt;
    bool m_waiting = false;
    std::uint64_t m_promptShownMs = 0;
};

/// PM_Profile, the profile manager: title `0x7b` and one item a row: `0x7d` use existing (code 0, when there is a
/// profile), `0x7c` create new (1, when there is none, or fewer than six and the save system has room), `0x7e` delete
/// (2, when there is a profile) and `0x7f` reload (3, always). Title and grid y follow the row count. Accept clears
/// delete mode, then: 0 → result 0 (PM_Load); 1 → 1 (PM_Create); 2 → delete mode and result 2 (PM_Load); 3 → the Lua
/// function `Menu.reloadProfiles`, stay.
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x0020bc80 PM_Profile::PM_Profile (PM_Profile.cpp)
class PmProfile final : public PmListScreen {
  public:
    static constexpr std::uint32_t kTitleString = 0x7b;
    static constexpr std::uint32_t kUseString = 0x7d;
    static constexpr std::uint32_t kCreateString = 0x7c;
    static constexpr std::uint32_t kDeleteString = 0x7e;
    static constexpr std::uint32_t kReloadString = 0x7f;
    /// The items' codes.
    static constexpr int kUse = 0;
    static constexpr int kCreate = 1;
    static constexpr int kDelete = 2;
    static constexpr int kReload = 3;

    explicit PmProfile(PmShared& shared) : PmListScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Profile"; }

  protected:
    /// @orig 0x0020be68 PM_Profile::Init (PM_Profile.cpp)
    void setUp() override;
    /// @orig 0x0020c588 PM_Profile::HandleCommand (PM_Profile.cpp)
    int accept(int code) override;
};

/// PM_Load, choose a profile: title `0x88` (or `0x7e` in delete mode) and one item per used slot (its name, code = the
/// slot), two a row: rows {n}, {2, n − 2} or {2, 2, n − 4} for n below 3, 3-4 and 5-6. Accept keeps the slot; in
/// delete mode, or when the save system reports the profile damaged, the result is 1 (PM_Delete); otherwise the
/// profile loads, is marked in use and the menus are done.
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x00209068 PM_Load::PM_Load (PM_Load.cpp)
class PmLoad final : public PmListScreen {
  public:
    static constexpr std::uint32_t kTitleString = 0x88;
    static constexpr std::uint32_t kDeleteTitleString = 0x7e;
    /// The result that leads to PM_Delete.
    static constexpr int kToDelete = 1;

    explicit PmLoad(PmShared& shared) : PmListScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Load"; }

  protected:
    /// @orig 0x00209250 PM_Load::Init (PM_Load.cpp)
    void setUp() override;
    /// @orig 0x002098b0 PM_Load::HandleCommand (PM_Load.cpp)
    int accept(int code) override;
};

/// PM_Continue: the chosen profile's name (size 2.0, grey, y 0.73) over a one-row grid of `0x7a` CONTINUE (0) and
/// `0x80` DELETE (1). CONTINUE loads and ends the menus like PM_Load; DELETE returns 1 (PM_Delete). No screen of the
/// PS2 build leads here (PM_Mode never returns 4).
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x00203110 PM_Continue::PM_Continue (PM_Continue.cpp)
class PmContinue final : public PmListScreen {
  public:
    static constexpr std::uint32_t kContinueString = 0x7a;
    static constexpr std::uint32_t kDeleteString = 0x80;
    static constexpr float kNameY = 0.73F;

    explicit PmContinue(PmShared& shared) : PmListScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Continue"; }

  protected:
    /// @orig 0x00203300 PM_Continue::Init (PM_Continue.cpp)
    void setUp() override;
    /// @orig 0x00203800 PM_Continue::HandleCommand (PM_Continue.cpp)
    int accept(int code) override;
    void drawExtras() override;
    void closeExtras() override { m_name.shutdown(); }

  private:
    TextWidget m_name;
};

/// PM_Delete: the chosen profile's name (y 0.73), the question `0x82` (or `0x83` for a damaged profile; size 0.85, red,
/// y 0.66) and a one-row grid of `0x84` YES and `0x85` NO, NO selected. YES deletes the profile, calls the Lua function
/// `Menu.deleteProfile` and returns 2 (PM_Greet) for a damaged profile, else 0 (PM_Profile); NO pops.
///
/// Research: docs/research/frontend.md#pm-screens
/// @orig 0x00205950 PM_Delete::PM_Delete (PM_Delete.cpp)
class PmDelete final : public PmListScreen {
  public:
    static constexpr std::uint32_t kSureString = 0x82;
    static constexpr std::uint32_t kDamagedString = 0x83;
    static constexpr std::uint32_t kYesString = 0x84;
    static constexpr std::uint32_t kNoString = 0x85;
    static constexpr float kNameY = 0.73F;
    static constexpr float kQuestionY = 0.66F;
    /// The Lua function YES calls.
    static constexpr std::string_view kDeleteFunction = "Menu.deleteProfile";

    explicit PmDelete(PmShared& shared) : PmListScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Delete"; }

  protected:
    /// @orig 0x00205b38 PM_Delete::Init (PM_Delete.cpp)
    void setUp() override;
    /// @orig 0x00206238 PM_Delete::HandleCommand (PM_Delete.cpp)
    int accept(int code) override;
    void drawExtras() override;
    void closeExtras() override;

  private:
    TextWidget m_name;
    TextWidget m_question;
    bool m_damaged = false;
};

} // namespace coney::gui
