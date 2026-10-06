// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/movie_player.h"

#include <cmath>
#include <format>

namespace coney {

std::optional<std::string> levelIntroMovie(const LevelRecord& record, double section) {
    if (record.values.at(LevelRecord::kIntroValue) == 0.0 || section >= 2.0) {
        return std::nullopt;
    }
    return std::format("L{}_IN", static_cast<int>(std::trunc(record.number)));
}

} // namespace coney
