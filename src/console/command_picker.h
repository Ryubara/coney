// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "console/console.h"
#include "console/registry.h"

// @coney-only the Coney console's command picker (docs/guides/studio/console.md#input); the original has no console.

namespace coney::console {

/// One press the picker takes: the d-pad (or the left stick), cross and circle.
enum class PickerInput : std::uint8_t {
    Up,     ///< The row above.
    Down,   ///< The row below.
    Left,   ///< The argument's value before (a list's previous entry, a number one less).
    Right,  ///< The argument's value after.
    Accept, ///< Cross: choose the command, go on to the next row, or run the line.
    Back,   ///< Circle: back to the commands; on the commands, close.
};

/// Which list the picker shows.
enum class PickerPage : std::uint8_t {
    Commands,  ///< The present commands, by group.
    Arguments, ///< The chosen command's arguments, one row each, then the row that runs the line.
};

/// One row as the front end draws it.
struct PickerRow {
    std::string label; ///< A command's name, or an argument's (`run` for the last row).
    std::string value; ///< A command's group, or the argument's value (empty when it has none yet).
    std::string help;  ///< A command's summary, or the argument's help.
};

/// `value` moved by `step`, as a whole number (`whole`) or a decimal one, the way the picker steps a number argument. A
/// value that does not read in full as a number of that kind (empty, a word, or a number with text after it such as
/// `1.5x`, or `12x` for a whole one) counts as 0.
[[nodiscard]] std::string steppedNumber(std::string_view value, int step, bool whole);

/// The gamepad's way to build and run a line without typing (docs/guides/studio/console.md#input): the commands by
/// group, then one row per argument, each a list filled by its type's completion or a number changed with left and
/// right, then a row that runs the line, which the terminal shows as if typed. A command without arguments runs as
/// soon as it is chosen. It reads the registry each time, so a command a mod adds or removes shows at once.
class CommandPicker {
  public:
    /// A picker over `console`, which must outlive it, at the first command.
    explicit CommandPicker(Console& console) : m_console(console) {}

    /// Back to the commands, at the first one: each time the console opens from the pad.
    void reset();
    /// Takes one press for `slot`'s player. Returns false when circle is pressed on the commands (the console should
    /// close), true otherwise.
    bool apply(PickerInput input, PlayerSlot slot = 0);

    /// The list shown.
    [[nodiscard]] PickerPage page() const { return m_page; }
    /// The selected row of rows().
    [[nodiscard]] std::size_t selected() const { return m_selected; }
    /// The rows of the list shown.
    [[nodiscard]] std::vector<PickerRow> rows() const;
    /// The line the arguments build: the command's name and each argument given so far, up to the first one left out
    /// (empty on the commands page).
    [[nodiscard]] std::string line() const;

  private:
    // The present commands in the order the picker lists them: by group, each group where its first command is.
    [[nodiscard]] std::vector<const Command*> commands() const;
    // The command chosen on the arguments page, or null when it went away (a mod stopped).
    [[nodiscard]] const Command* chosen() const;
    // The values argument `index` of the chosen command may take after the ones before it; empty when its type has
    // no list.
    [[nodiscard]] std::vector<std::string> choices(std::size_t index) const;
    // Moves argument `index`'s value one step (`step` -1 or 1).
    void stepValue(std::size_t index, int step);
    // Chooses the selected command: its arguments, or at once when it has none.
    void choose(PlayerSlot slot);

    Console& m_console;
    PickerPage m_page = PickerPage::Commands;
    std::size_t m_selected = 0;
    std::optional<CommandId> m_command; // the command on the arguments page
    std::vector<std::string> m_values;  // its arguments' values, one each, empty when not given
};

} // namespace coney::console
