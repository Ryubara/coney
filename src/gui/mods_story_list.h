// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/story_choices.h"
#include "core/story_graph.h"
#include "core/story_merge.h"
#include "core/story_view.h"
#include "gui/menu_input.h"

// The Mods screen's Story list: the model behind it, which the screen draws and drives with the menu commands the pad
// gives. Coney's own; the original has no mods.
//
// Research: docs/guides/studio/mission-mods.md#graph-mods-screen

namespace coney::gui {

/// The Mods screen's story view: the merged chain with each mod's missions under its title, and the player's two
/// choices on it, *Swap* on a shared anchor and the winner of two replacements, kept in `enabled.toml` with the
/// package hash of every mod they involve.
///
/// A direction moves the cursor over the rows it can stop on, accept does the row's action
/// (story::StoryViewRow::Action) and back closes the list. An action changes the merge at once (the chain is rebuilt
/// and the rows with it, the cursor staying on the same mission) and writes the file; a file that cannot be written
/// leaves the choice in force for the session and says so in message(). A file whose choices could not be read is never
/// written over (that would delete the tables the player wrote, the valid ones too): the choice holds for the session
/// only, and message() says the file was left as it is.
///
/// **Ownership (networking checklist):** menu state of the front end, not simulation state. What it decides is the
/// order of the merge, which the session builds once when the mods start; the choices are part of the mod list a
/// co-op session compares (the merged graph's hash). Ids, no pointers.
///
/// Research: docs/guides/studio/mission-mods.md#graph-mods-screen
class ModsStoryList {
  public:
    /// What a command did.
    enum class Result : std::uint8_t {
        Nothing, ///< Nothing happened (a direction at an end, accept on a row with no action).
        Moved,   ///< The cursor moved.
        Swapped, ///< Two missions on one anchor changed places.
        Chose,   ///< A refused replacement took the place.
        Closed,  ///< Back: the list closes.
    };

    /// The list over `seed` (the original's chain) and the enabled story `mods` in load order, with the choices read
    /// from `enabledFile` (none when it does not exist). The cursor is on the first row it can stop on. A file that
    /// cannot be read leaves no choices and says so in message().
    ModsStoryList(story::StoryGraph seed, std::vector<story::StoryMod> mods, std::filesystem::path enabledFile);

    /// Whether the screen shows the list: any enabled mod has a `[story]` section, missions or none.
    [[nodiscard]] bool shown() const { return m_merge.modded; }

    /// Applies one menu command.
    Result handle(MenuCommand command);

    /// The rows, top to bottom.
    [[nodiscard]] std::span<const story::StoryViewRow> rows() const { return m_rows; }
    /// The index of the row under the cursor; nothing when no row can be stopped on.
    [[nodiscard]] std::optional<std::size_t> cursor() const { return m_cursor; }
    /// The merge the rows are built from: the chain, the placed and refused missions and the groups.
    [[nodiscard]] const story::StoryMerge& merge() const { return m_merge; }
    /// The choices in force, as they are written to the file.
    [[nodiscard]] std::span<const story::StoryChoice> choices() const { return m_choices; }
    /// What went wrong with the file last, for the screen to show; empty when nothing did.
    [[nodiscard]] const std::string& message() const { return m_message; }

  private:
    // Rebuilds the merge and the rows from the choices, and puts the cursor on the row of `keep` (else the first row it
    // can stop on).
    void rebuild(std::string_view keep);
    // Moves the cursor to the next (or previous) row it can stop on; false at an end.
    bool move(bool down);
    // Puts `order` as the choice for `group` (dropping the choice when it is the default order), rebuilds and writes.
    void choose(const story::StoryGroup& group, const std::vector<std::string>& order, std::string_view keep);
    // Writes the choices to the file, unless its choices could not be read; the message says when it was not written.
    void save();

    story::StoryGraph m_seed;
    std::vector<story::StoryMod> m_mods;
    std::filesystem::path m_file;
    std::vector<story::StoryChoice> m_choices;
    story::StoryMerge m_merge;
    std::vector<story::StoryViewRow> m_rows;
    std::optional<std::size_t> m_cursor;
    std::string m_message;
    bool m_unreadable = false; // the file's choices did not read: it is left as it is
};

} // namespace coney::gui
