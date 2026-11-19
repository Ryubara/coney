// SPDX-License-Identifier: GPL-3.0-or-later
// The level's simulation in play (PlayLevelSimulation): player 1 made on the ground at its start, the run's HUD, and
// the level number the scene's models follow. The steps are in the files beside this one (play_level_cast.cpp,
// play_level_objects.cpp, play_level_throw_aim.cpp, play_level_held.cpp, play_level_hats.cpp,
// play_level_props.cpp); the platform's play mode (src/platform/play_level_mode.h) runs them.
#include "gamemodes/play_level_simulation.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "human/player.h"

namespace coney {

namespace {

// The level number of a scene called `name` (99 for `level99`), which picks the Armies of the Night models; 0 for a
// sandbox or any other name.
// @coney-only Coney's reading of the level number from the scene's name
int levelNumberOf(std::string_view name) {
    constexpr std::string_view kPrefix = "level";
    if (!name.starts_with(kPrefix) || name.size() == kPrefix.size()) {
        return 0;
    }
    int number = 0;
    for (const char c : name.substr(kPrefix.size())) {
        if (c < '0' || c > '9' || number > 9999) {
            return 0;
        }
        number = (number * 10) + (c - '0');
    }
    return number;
}

} // namespace

PlayLevelSimulation::PlayLevelSimulation(const io::Wad& wad, std::unique_ptr<world::LevelGround> ground,
                                         std::unique_ptr<human::PlayerCharacter> character,
                                         std::function<void(std::string_view)> print, std::string model,
                                         const characters::CharacterTypes& types, int type, bool snapToGround)
    : m_wad(wad), m_scenery(std::move(ground)), m_character(std::move(character)), m_types(types), m_type(type),
      m_levelNumber(levelNumberOf(m_scenery->name())),
      // A start with no snap is spawned without the mesh; the first update's ground snap then settles the feet.
      m_player(std::make_unique<human::Player>(*m_character, snapToGround ? &m_scenery->collision() : nullptr,
                                               m_scenery->start(), human::playerClassOf(types, type))),
      m_print(std::move(print)), m_model(std::move(model)) {}

// The platform's mode takes the AI out of the player's step and unbinds the level's objects before its own parts go
// (platform::PlayLevelMode::~PlayLevelMode()); what is left goes in reverse order of its declaration.
PlayLevelSimulation::~PlayLevelSimulation() = default;

void PlayLevelSimulation::useHud(hud::Hud& shared) {
    m_hud->useHud(shared);
    shared.attachPlayer(0, m_type);
}

} // namespace coney
