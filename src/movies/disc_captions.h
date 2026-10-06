// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/language.h"
#include "fileio/wad.h"
#include "movies/movie_mode.h"

namespace coney::movies {

/// The name a language's section of the Subtitles chunk starts with: `ENGLISH`, `SPANISH`, `FRENCH`, `ITALIAN` or
/// `GERMAN`.
[[nodiscard]] std::string_view captionLanguageName(Language language);

/// The level file whose Subtitles chunk holds the captions of movie `name`: `level<n>.lev` for `L<n>_IN` and
/// `L<n>_OUT`; nothing for another movie. **Coney stand-in**: the original uses whichever level file is loaded when
/// the movie plays (level1 at start-up, level100 in the front end, the level itself for its intro and outro); the
/// level a movie is named after holds the same section (checked for `l1_in_sub` in level1 and level100).
[[nodiscard]] std::optional<std::string> captionLevelFile(std::string_view name);

/// The captions of each movie from `wad`: the scene `<movie>_sub` (lower case) for the timing, and the Subtitles
/// chunk of captionLevelFile() in the section of `language()` for the text. Nothing for a movie with no such scene
/// or no section for it. The WAD must outlive the source.
/// Research: docs/research/movies.md#captions
[[nodiscard]] CaptionSource wadCaptionSource(const io::Wad& wad, std::function<Language()> language);

} // namespace coney::movies
