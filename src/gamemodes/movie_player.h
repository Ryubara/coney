// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "warriors/level_table.h"

namespace coney {

/// The movie player as the game modes ask it (`Movie_Play`, `0x0042a938`): play the movie `name` (`LOGO`, `L1_IN`,
/// `L99_IN`) to its end or until it is skipped. The original blocks until the movie ends, stops the music
/// first and leaves the screen black after it (docs/research/frontend.md#movies). movies::MovieMode implements it
/// (src/movies/movie_mode.h); without one attached, FrontEndServices skips each movie as if it had ended at once.
///
/// Research: docs/research/frontend.md#movies, docs/research/boot.md#main
class MoviePlayer {
  public:
    virtual ~MoviePlayer() = default;
    MoviePlayer() = default;
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;
    MoviePlayer(MoviePlayer&&) = delete;
    MoviePlayer& operator=(MoviePlayer&&) = delete;

    /// Plays the movie `name` and returns when it has ended.
    /// @orig 0x0042a938 Movie_Play (unknown)
    virtual void playMovie(std::string_view name) = 0;
};

/// The intro movie `InitLevel` plays for a level (`L<n>_IN`, n the record's level number) when the record asks for one
/// (flag `0x02` at `+0x0d`) and `section` (`W_GameState + 0x33a`, the checkpoint) is the first, below 2; nothing
/// otherwise. `level99`, checkpoint 1, gives `L99_IN`.
///
/// Research: docs/research/level-loading.md#initlevel (step 12), docs/research/frontend.md#story-start
/// @orig 0x0015fe90 InitLevel (InitLevel.cpp)
[[nodiscard]] std::optional<std::string> levelIntroMovie(const LevelRecord& record, double section);

} // namespace coney
