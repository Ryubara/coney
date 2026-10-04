// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace coney {

/// The game's languages, in the order of the original's language field (`W_GameState + 0x120`), which is also what
/// the scripts' `GetLanguage()` returns (docs/research/gui.md#strings).
enum class Language : std::uint8_t {
    English,
    Spanish,
    French,
    Italian,
    German,
};

/// The two-letter code the game's files use for `language`: `en`, `es`, `fr`, `it` or `de` (as in
/// `config_strings_en.lua`).
[[nodiscard]] constexpr std::string_view languageCode(Language language) {
    switch (language) {
    case Language::English:
        return "en";
    case Language::Spanish:
        return "es";
    case Language::French:
        return "fr";
    case Language::Italian:
        return "it";
    case Language::German:
        return "de";
    }
    return "en";
}

/// The language whose code (languageCode()) is `code`, or nothing for another string.
[[nodiscard]] constexpr std::optional<Language> languageFromCode(std::string_view code) {
    for (const Language language :
         {Language::English, Language::Spanish, Language::French, Language::Italian, Language::German}) {
        if (languageCode(language) == code) {
            return language;
        }
    }
    return std::nullopt;
}

} // namespace coney
