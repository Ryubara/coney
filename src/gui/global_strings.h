// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace coney::gui {

/// The tables of UI text the game's configuration scripts fill, one per table of `GSTRING` and the binding that
/// receives it (docs/research/gui.md#strings).
enum class StringTable : std::uint8_t {
    Hud,      ///< `GSTRING.HUD` through `CfgHUDMessage`: the global strings menus and the HUD show by id.
    Crime,    ///< `GSTRING.CRIME` through `CfgCrimeMessage`.
    Tutorial, ///< `TSTRING` through `CfgTutorialMessage`.
    Command,  ///< `GSTRING.COMMAND` through `CfgWarriorCommand`.
    Announce, ///< `GSTRING.ANNOUNCE` through `CfgAnnounceMessage`.
};

/// How many StringTable values there are.
inline constexpr std::size_t kStringTableCount = 5;

/// The game's UI strings by table and id, as the language's `config_strings_<code>.lua` sets them. Text is kept as the
/// script's raw bytes, markup tags included (docs/research/gui.md#markup).
///
/// The original keeps the HUD table as an array of pointers at `0x00600048` (id × 4) into the Lua pool; the storage of
/// the other four is not researched. Coney keeps every table as a map, so any id works: Coney's choice until the
/// array's size is known.
///
/// Research: docs/research/gui.md#strings
class GlobalStrings {
  public:
    /// The HUD string `id`, or an empty string when the scripts set none. The view lives until the string is set
    /// again or this object is destroyed.
    /// @orig 0x0019ee70 GlobalString_Get (unknown)
    [[nodiscard]] std::string_view get(std::uint32_t id) const { return get(StringTable::Hud, id); }

    /// Sets the HUD string `id` to `text`, replacing any earlier one.
    /// @orig 0x0019eea0 GlobalString_Set (unknown)
    void set(std::uint32_t id, std::string text) { set(StringTable::Hud, id, std::move(text)); }

    /// String `id` of `table`, or an empty string when none is set.
    [[nodiscard]] std::string_view get(StringTable table, std::uint32_t id) const;
    /// Sets string `id` of `table`.
    void set(StringTable table, std::uint32_t id, std::string text);
    /// How many strings `table` holds.
    [[nodiscard]] std::size_t size(StringTable table) const { return tableOf(table).size(); }
    /// The ids and strings of `table`, by ascending id.
    [[nodiscard]] const std::map<std::uint32_t, std::string>& entries(StringTable table) const {
        return tableOf(table);
    }

  private:
    // The map behind `table`.
    [[nodiscard]] const std::map<std::uint32_t, std::string>& tableOf(StringTable table) const {
        return m_tables[static_cast<std::size_t>(table)];
    }

    std::array<std::map<std::uint32_t, std::string>, kStringTableCount> m_tables;
};

} // namespace coney::gui
