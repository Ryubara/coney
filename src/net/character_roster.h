// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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

    /// The character to draw a human whose snapshot field is `id` with: his own, loaded now when no human of it has
    /// been drawn yet; the fallback for kNoCharacter, an id outside the field's range, or a character that did not
    /// load.
    [[nodiscard]] const Character& of(std::int32_t id) {
        const std::optional<std::size_t> index = characterListIndexOf(id);
        if (!index) {
            return *m_fallback;
        }
        auto found = m_loaded.find(*index);
        if (found == m_loaded.end()) {
            // The first human of this character: load it, and keep the answer, a failure too, so it is said once.
            auto loaded = m_load(*index);
            if (!loaded) {
                m_print(std::format("client: character {} did not load ({}); its humans are drawn as the fallback\n",
                                    *index, loaded.error().message));
            }
            found = m_loaded.emplace(*index, loaded ? std::move(*loaded) : nullptr).first;
        }
        return found->second != nullptr ? *found->second : *m_fallback;
    }

    /// The fallback character.
    [[nodiscard]] const Character& fallback() const { return *m_fallback; }
    /// How many characters besides the fallback are loaded.
    [[nodiscard]] std::size_t loaded() const {
        return static_cast<std::size_t>(
            std::ranges::count_if(m_loaded, [](const auto& entry) { return entry.second != nullptr; }));
    }

  private:
    std::unique_ptr<Character> m_fallback;
    Loader m_load;
    Print m_print;
    std::map<std::size_t, std::unique_ptr<Character>> m_loaded; // null: it did not load
};

} // namespace coney::net
