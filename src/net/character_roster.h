// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <utility>

#include "core/error.h"
#include "net/character_id.h"

// The characters a client has loaded for the humans a snapshot names, by id. Coney's own; the original has no network
// play (docs/guides/client-world.md#characters).

namespace coney::net {

/// The characters a client draws its humans with: the fallback it always has, and each character a snapshot names,
/// loaded the first time a human of it is drawn and kept. `Character` is whatever the client draws with (its model,
/// textures and clips); the roster only chooses and keeps.
///
/// A human with no character (kNoCharacter), or whose character does not load, gets the fallback, **Coney's choice**
/// (as the play mode draws a cast human whose model fails to load); the failure is printed once and kept, so it is not
/// tried again every frame.
/// @coney-only network play; the original has none
template <typename Character> class CharacterRoster {
  public:
    /// Loads the character at a Character List index.
    using Loader = std::function<std::expected<std::unique_ptr<Character>, Error>(std::size_t listIndex)>;
    /// Takes a log line.
    using Print = std::function<void(std::string_view)>;

    /// A roster whose fallback is `fallback` (never null), loading others with `load` and logging with `print`.
    CharacterRoster(std::unique_ptr<Character> fallback, Loader load, Print print)
        : m_fallback(std::move(fallback)), m_load(std::move(load)), m_print(std::move(print)) {}

    /// The character to draw a human whose snapshot field is `id` with: stub, always the fallback.
    [[nodiscard]] const Character& of(std::int32_t id) {
        (void)id;
        return *m_fallback;
    }

    /// The fallback character.
    [[nodiscard]] const Character& fallback() const { return *m_fallback; }
    /// How many characters besides the fallback are loaded.
    [[nodiscard]] std::size_t loaded() const { return 0; }

  private:
    std::unique_ptr<Character> m_fallback;
    Loader m_load;
    Print m_print;
    std::map<std::size_t, std::unique_ptr<Character>> m_loaded; // null: it did not load
};

} // namespace coney::net
