// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "gamemodes/level_crime_services.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/object_services.h"

namespace coney {

/// What gameplay gives the level's glass panes, doors and barriers of the world they change beyond its triangles and
/// links: the lock pick's script callbacks, the `CrimeScene` flag a break-in moves, and the sounds, which go to
/// `sounds` (the audio's ObjectSounds; null: none), and, once setPlayers() gives it the game state, the crime reports
/// (through LevelCrimeServices) and the players' statistics. The rest of ObjectServices (shards, loose objects, models)
/// does nothing until those systems are in Coney.
///
/// Research: docs/research/objects.md#coneys-implementation, docs/research/crimes.md#lockpick
class LevelObjectServices final : public world_objects::ObjectServices {
  public:
    /// Over `scripts` and `flags`, which must outlive it; sounds to `sounds` (may be null).
    LevelObjectServices(script::ScriptSystem& scripts, world_objects::WorldFlags& flags,
                        world_objects::ObjectServices* sounds)
        : m_scripts(scripts), m_flags(flags), m_sounds(sounds),
          m_crimes(scripts, [this](const CrimePosition& at) { moveCrimeSceneFlag(anim::Vec3{at[0], at[1], at[2]}); }) {}

    /// Crimes and statistics go to `state`'s players, whose humans are `humans` (both may be null: none); both must
    /// outlive their use.
    void setPlayers(GameState* state, CreatedHumans* humans);
    /// What a crime report reaches in the level.
    [[nodiscard]] CrimeServices& crimeServices() { return m_crimes; }

    /// Sounds go to `sounds` from now on (null: none).
    void setSounds(world_objects::ObjectServices* sounds) { m_sounds = sounds; }

    void playSound(std::uint32_t nameHash, anim::Vec3 at) override;
    void playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 at) override;
    void lockPickClick(double human) override;
    /// Calls the script function `function` with the human's and the door's handles; nothing for an empty name.
    void callScript(std::string_view function, double human, double door) override;
    /// Moves the level's `CrimeScene` flag (kCrimeSceneFlag) to `at`.
    void moveCrimeSceneFlag(anim::Vec3 at) override;
    /// Reports the crime to the game state's crime reports at the scripts' time, asking for responders.
    void reportCrime(int type, anim::Vec3 at, double offender) override;
    /// Scores statistic `category`-`event` for `human` when he is a player.
    void scoreEvent(double human, int category, int event) override;
    /// Crime statistic 4-10 for a player who broke a pane (a player's gang member is not told apart yet).
    void countPaneBroken(double breaker) override;

  private:
    script::ScriptSystem& m_scripts;
    world_objects::WorldFlags& m_flags;
    world_objects::ObjectServices* m_sounds;
    LevelCrimeServices m_crimes;
    GameState* m_state = nullptr;
    CreatedHumans* m_humans = nullptr;

    // The 0-based player whose human is `human`; -1 for none.
    [[nodiscard]] int playerOf(double human) const;
};

} // namespace coney
