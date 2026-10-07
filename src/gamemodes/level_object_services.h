// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

#include "animation/anim_math.h"
#include "effects/particles.h"
#include "gamemodes/level_crime_services.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/cars.h"
#include "world_objects/flags.h"
#include "world_objects/object_services.h"

namespace coney {

/// What gameplay gives the level's glass panes, doors and barriers of the world they change beyond its triangles and
/// links: the lock pick's script callbacks, the `CrimeScene` flag a break-in moves, and the sounds, which go to
/// `sounds` (the audio's ObjectSounds; null: none), and, once setPlayers() gives it the game state, the crime reports
/// (through LevelCrimeServices) and the players' statistics, and once setParticles() gives it the level's particles,
/// the panes' shards and the objects' dust. The rest of ObjectServices (loose objects, models) does nothing until those
/// systems are in Coney.
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
    /// Car stereos are freed in `cars` from now on (null: none).
    void setCars(world_objects::Cars* cars) { m_cars = cars; }
    /// Shards, dust and bursts go to `particles` from now on (null: none), culled round where `player` says player 1
    /// is (empty: not culled by distance).
    void setParticles(effects::ParticleSystems* particles, std::function<std::optional<anim::Vec3>()> player) {
        m_particles = particles;
        m_player = std::move(player);
    }

    /// Damage a human does goes to `damage` from now on (empty: nowhere): gameplay sends the boxes' message 6.
    void setDamageReceiver(std::function<void(double human, double object)> damage) { m_damage = std::move(damage); }
    /// Passes the damage to the receiver (setDamageReceiver()).
    void damageDone(double human, double object) override {
        if (m_damage) {
            m_damage(human, object);
        }
    }

    /// A car's hit reports go to `hit` from now on (empty: nowhere): gameplay sends the car's message 0x19.
    void setCarHitReceiver(std::function<void(double car, double human, int part, bool broke)> hit) {
        m_carHit = std::move(hit);
    }
    /// Passes the report to the receiver (setCarHitReceiver()).
    void carHit(double car, double human, int part, bool broke) override {
        if (m_carHit) {
            m_carHit(car, human, part, broke);
        }
    }

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
    /// Whether a shatter makes shards: room in the particle budget for the most a shatter makes, and player 1 within
    /// 10 m of `centre`. **Coney's stand-in**: the two tests' points (15 m and 10 m) are not on the page, so player 1
    /// stands for both.
    [[nodiscard]] bool shardsWanted(anim::Vec3 centre) override;
    /// A `glasstest` shard (effects::ParticleSystems::spawnShard()).
    void spawnShard(anim::Vec3 at, float size, std::uint32_t colour) override;
    /// Frees the stereo of every car within `radius` of `at` whose stereo sits there (a car window broken).
    /// Where a stereo sits: world_objects::Cars::stereoPosition().
    void freeCarStereos(anim::Vec3 at, float radius) override;
    /// Dust: a `sub_shack_puff`. **Coney's stand-in**: which types `0x003c57d8` makes is not traced; `radius` is not
    /// used.
    void dust(anim::Vec3 at, float radius) override;
    /// A leaf's burst: a `sub_shack_puff` (**Coney's stand-in**, as dust()).
    void burst(anim::Vec3 at) override;

  private:
    script::ScriptSystem& m_scripts;
    world_objects::WorldFlags& m_flags;
    world_objects::ObjectServices* m_sounds;
    effects::ParticleSystems* m_particles = nullptr;
    std::function<std::optional<anim::Vec3>()> m_player;
    std::function<void(double, double)> m_damage;            // setDamageReceiver()
    std::function<void(double, double, int, bool)> m_carHit; // setCarHitReceiver()
    LevelCrimeServices m_crimes;
    GameState* m_state = nullptr;
    CreatedHumans* m_humans = nullptr;

    // The 0-based player whose human is `human`; -1 for none.
    [[nodiscard]] int playerOf(double human) const;
    world_objects::Cars* m_cars = nullptr;
};

} // namespace coney
