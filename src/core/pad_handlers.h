// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace coney {

/// The Lua pad handlers (`0x005dda90`: 8 pad records × 16 buttons): a script function per button that the pad update
/// calls when the button goes down, unless the pause mode (0xa) is on top; menus and pop-ups made in Lua use them. And
/// the one global handler of `PadSetHandlerEx` (`0x0050b73c`), which a player-input routine (`0x001480e0`) calls when
/// the player acts on a target. The table keeps names only; the caller looks them up and calls them.
///
/// Research: docs/research/frontend.md#input, docs/references/bindings/input.md
class PadHandlers {
  public:
    /// Pad records.
    static constexpr std::size_t kRecords = 8;
    /// Buttons per record: one per bit of the button word.
    static constexpr std::size_t kButtons = 16;

    /// `PadSetHandler(player, button, callback)`: the handler of `record`'s button `mask` is `function` (empty
    /// removes it). It is stored under the highest set bit of `mask`; a mask of 0 or a record past the table does
    /// nothing.
    /// @orig 0x00145698 Pad_SetLuaHandler (unknown)
    void set(std::size_t record, std::uint16_t mask, std::string function);
    /// The handler of `record`'s button bit `bit` (0-15); empty for none.
    [[nodiscard]] const std::string& handler(std::size_t record, std::size_t bit) const;
    /// The handlers due for `record` when the buttons `pressed` went down this sample, lowest bit first.
    [[nodiscard]] std::vector<std::string> due(std::size_t record, std::uint16_t pressed) const;
    /// Removes every handler of every record (a menu's `Enter` resets them).
    void clear();

    /// `PadSetHandlerEx(callback)`: the global handler (empty removes it).
    /// @orig 0x00148210 Pad_SetLuaHandlerEx (unknown)
    void setTargetHandler(std::string function) { m_targetHandler = std::move(function); }
    /// The global handler; empty for none.
    [[nodiscard]] const std::string& targetHandler() const { return m_targetHandler; }

  private:
    std::array<std::array<std::string, kButtons>, kRecords> m_handlers;
    std::string m_targetHandler;
};

} // namespace coney
