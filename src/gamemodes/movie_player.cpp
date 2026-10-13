// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/movie_player.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

#include "fileio/disc.h"
#include "gamemodes/front_end_services.h"

namespace coney {

namespace {

// The disc's movie folder and the extension of its files (docs/research/movies.md#the-movies).
constexpr std::string_view kMovieFolder = "PSS";
constexpr std::string_view kMovieExtension = ".BIK";

// A level's movie, read from its name: `L<n>_IN` or `L<n>_OUT`.
struct LevelMovie {
    int level = 0;
    bool outro = false;
};

// `name` as a level's movie, if it is one.
std::optional<LevelMovie> levelMovie(std::string_view name) {
    if (!name.starts_with('L')) {
        return std::nullopt;
    }
    const std::size_t underscore = name.find('_');
    if (underscore == std::string_view::npos || underscore < 2) {
        return std::nullopt;
    }
    const std::string_view suffix = name.substr(underscore + 1);
    if (suffix != "IN" && suffix != "OUT") {
        return std::nullopt;
    }
    int level = 0;
    const char* first = name.data() + 1;
    const char* last = name.data() + underscore;
    const auto [end, error] = std::from_chars(first, last, level);
    if (error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return LevelMovie{.level = level, .outro = suffix == "OUT"};
}

// Where a start-up movie comes in kStartUpMovies; the list's size for any other.
std::size_t startUpIndex(std::string_view name) {
    const auto found = std::ranges::find(kStartUpMovies, name);
    return static_cast<std::size_t>(std::distance(kStartUpMovies.begin(), found));
}

} // namespace

std::optional<std::string> levelIntroMovie(const LevelRecord& record, double section) {
    if (record.values.at(LevelRecord::kIntroValue) == 0.0 || section >= 2.0) {
        return std::nullopt;
    }
    return std::format("L{}_IN", static_cast<int>(std::trunc(record.number)));
}

std::vector<std::string> discMovies(const io::Disc& disc) {
    std::vector<std::string> names;
    for (const std::string& file : disc.filesIn(kMovieFolder)) {
        if (file.size() > kMovieExtension.size() && file.ends_with(kMovieExtension)) {
            names.push_back(file.substr(0, file.size() - kMovieExtension.size()));
        }
    }
    sortMovies(names);
    return names;
}

void sortMovies(std::vector<std::string>& names) {
    // The key: the group (start-up, other, level), then the start-up order, the level and intro-first, then the name.
    const auto key = [](const std::string& name) {
        if (const std::optional<LevelMovie> level = levelMovie(name)) {
            return std::tuple(2, std::size_t{0}, level->level, level->outro, std::string_view(name));
        }
        const std::size_t startUp = startUpIndex(name);
        return std::tuple(startUp < kStartUpMovies.size() ? 0 : 1, startUp, 0, false, std::string_view(name));
    };
    std::ranges::sort(names, [&key](const std::string& a, const std::string& b) { return key(a) < key(b); });
}

std::string movieUse(std::string_view name) {
    std::string use = startUpIndex(name) < kStartUpMovies.size() ? "start-up" : "";
    if (const std::optional<LevelMovie> level = levelMovie(name)) {
        use += std::format("{}level{} {}", use.empty() ? "" : ", ", level->level, level->outro ? "outro" : "intro");
    }
    return use;
}

} // namespace coney
