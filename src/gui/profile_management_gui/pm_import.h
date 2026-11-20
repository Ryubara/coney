// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the profile manager's import screen; the original has no import (docs/research/frontend.md#pm-import)
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "gui/profile_management_gui/pm_list_screen.h"
#include "gui/text_widget.h"
#include "warriors/save_import.h"

namespace coney::gui {

/// PM_Import, Coney's screen for bringing a profile over from a PS2 or Xbox save: PM_Profile's IMPORT PROFILE item
/// leads here when the save system's import folder (ProfileStore::importFolder()) holds something and a slot is free.
/// It is laid out as PM_Load: the title, then one item per profile found (its name; code = its place in the list), two
/// a row, at most six. A line above the title says where the selected profile comes from (grey), or why nothing can be
/// imported (red). Accept imports the profile into the first free slot (warriors/save_import.h) and returns kToProfile,
/// back to PM_Profile, which then offers it under USE EXISTING PROFILE; an import that is refused (the name is taken,
/// no free slot) plays the refused cue and shows why, in red, and the screen stays. Back pops.
///
/// The text is Coney's own, in English for every language: the game's string tables have nothing for it.
///
/// Research: docs/research/frontend.md#pm-import, docs/research/save.md#import
class PmImport final : public PmListScreen {
  public:
    /// PM_Profile's item and this screen's title.
    static constexpr std::string_view kImportText = "IMPORT PROFILE";
    /// The most profiles the grid lists (PM_Load's three rows of two).
    static constexpr std::size_t kMaxListed = 6;
    /// The result that goes back to PM_Profile after an import.
    static constexpr int kToProfile = 0;
    /// The line's y: above the title of a three-row grid (0.65), as PM_Delete's question sits above its grid.
    static constexpr float kLineY = 0.58F;

    /// A screen over `shared`, which must outlive it.
    explicit PmImport(PmShared& shared) : PmListScreen(shared) {}
    [[nodiscard]] std::string_view name() const override { return "PM_Import"; }

    /// What the import folder held when the screen opened.
    [[nodiscard]] const ImportScan& scan() const { return m_scan; }
    /// The line above the title, for tests.
    [[nodiscard]] const std::string& line() const { return m_lineText; }

    /// Whether PM_Profile offers IMPORT PROFILE: the save system has an import folder that holds something, and a
    /// slot is free.
    [[nodiscard]] static bool offered(const PmShared& shared);

  protected:
    /// Reads the import folder and lists the profiles found.
    void setUp() override;
    /// Imports the chosen profile.
    int accept(int code) override;
    /// A move updates the line for the newly selected profile.
    int direction(MenuCommand command) override;
    void drawExtras() override;
    void closeExtras() override { m_line.shutdown(); }

  private:
    // Shows `text` on the line above the title, red for a problem, grey otherwise.
    void showLine(std::string text, bool problem);
    // The line for the selected profile: its save and where in it.
    void describeSelected();

    ImportScan m_scan;
    std::vector<ImportCandidate> m_listed;
    std::vector<std::string> m_from; // Each listed profile's line: its save and where in it.
    TextWidget m_line;
    std::string m_lineText;
};

} // namespace coney::gui
