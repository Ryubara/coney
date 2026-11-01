// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/save_game.h"

namespace coney {

// Stub for the test commit: writes nothing.
std::vector<std::byte> SaveGame::serialise() const { return {}; }

// Stub for the test commit: reads nothing.
std::expected<SaveGame, Error> SaveGame::parse(std::span<const std::byte> /*bytes*/) {
    return fail(ErrorCode::Invalid, "not written yet");
}

// Stub for the test commit: no name.
std::string SaveGame::nameOf(std::span<const std::byte> /*bytes*/) { return {}; }

} // namespace coney
