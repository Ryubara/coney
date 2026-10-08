// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "effects/particles.h"
#include "gamemodes/level_crime_services.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/cars.h"
#include "world_objects/flags.h"
#include "world_objects/object_services.h"
#include "world_objects/spawn_records.h"

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
          m_crimes(
              scripts, [this](const CrimePosition& at) { moveCrimeSceneFlag(anim::Vec3{at[0], at[1], at[2]}); },
              [this](const CrimePosition& at, int gang) { robStore(anim::Vec3{at[0], at[1], at[2]}, gang); }) {}

    /// Crimes and statistics go to `state`'s players, whose humans are `humans` (both may be null: none); both must
    /// outlive their use.
    void setPlayers(GameState* state, CreatedHumans* humans);
    /// What a crime report reaches in the level.
    [[nodiscard]] CrimeServices& crimeServices() { return m_crimes; }
    /// The offenders' gangs come from `brains` from now on (null: none); it must outlive its use.
    void setBrains(const ai::ScriptServices* brains) { m_crimes.setBrains(brains); }
    /// Where player 1's crime messages go (LevelCrimeServices::setHud()).
    void setCrimeHud(std::function<void(int message)> notify) { m_crimes.setHud(std::move(notify)); }

    /// Sounds go to `sounds` from now on (null: none).
    void setSounds(world_objects::ObjectServices* sounds) { m_sounds = sounds; }
    /// Loose objects (a broken prop's piece) become spawn records in `records` from now on (null: none).
    void setSpawnRecords(world_objects::SpawnRecords* records) { m_records = records; }
    /// Car stereos are freed in `cars` from now on (null: none).
    void setCars(world_objects::Cars* cars) { m_cars = cars; }
    /// Shards, dust and bursts go to `particles` from now on (null: none), culled by the camera `view` gives for
    /// player 1's view (empty, or nothing yet: not culled by the camera).
    void setParticles(effects::ParticleSystems* particles, std::function<std::optional<camera::CameraView>()> view) {
        m_particles = particles;
        m_view = std::move(view);
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
    /// A break-in at `at` by gang `gang` (-1 none; `GameState_ReportCrime`, docs/research/crimes.md#stores): the
    /// nearest store flag (activity 14) within 10 m is marked robbed (group bit 16, the gang in bits 18-22), and the
    /// particle system nearest it within 6 m whose type name contains `strobe` (its alarm) gets message `0x12`.
    /// **Coney's stand-in**: the store's buyers and browsers are not switched off yet.
    void robStore(anim::Vec3 at, int gang);
    /// Reports the crime to the game state's crime reports at the scripts' time, asking for responders.
    void reportCrime(int type, anim::Vec3 at, double offender) override;
    /// Scores statistic `category`-`event` for `human` when he is a player.
    void scoreEvent(double human, int category, int event) override;
    /// Crime statistic 4-10 for a player who broke a pane (a player's gang member is not told apart yet).
    void countPaneBroken(double breaker) override;
    /// Whether a shatter makes shards: room in the particle budget for the most a shatter makes, and player 1's camera
    /// within 15 m of `centre` with `centre` in its view by a 10 m margin (effects::effectNearView()).
    /// @orig 0x003e4cb8 SubGlass_Update (unknown)
    [[nodiscard]] bool shardsWanted(anim::Vec3 centre) override;
    /// A `glasstest` shard (effects::ParticleSystems::spawnShard()).
    void spawnShard(anim::Vec3 at, float size, std::uint32_t colour) override;
    /// Frees the stereo of every car within `radius` of `at` whose stereo sits there (a car window broken).
    /// Where a stereo sits: world_objects::Cars::stereoPosition().
    void freeCarStereos(anim::Vec3 at, float radius) override;
    /// Dust: a `sub_shack_puff`. **Coney's stand-in**: which types `0x003c57d8` makes is not traced; `radius` is not
    /// used.
    void dust(anim::Vec3 at, float radius) override;
    /// A loose object: a spawn record of `type` at `at` turned by `rotation`, in zone 0 and untinted, with the next
    /// world object handle; kNoObject without records or a handle. **Coney's stand-in**: it rests where it is made
    /// (no knock or fall, docs/research/physics.md#movers).
    double spawnObject(std::string_view type, anim::Vec3 at, anim::Quat rotation) override;
    /// A leaf's burst: a `sub_shack_puff` (**Coney's stand-in**, as dust()).
    void burst(anim::Vec3 at) override;

  private:
    script::ScriptSystem& m_scripts;
    world_objects::WorldFlags& m_flags;
    world_objects::ObjectServices* m_sounds;
    effects::ParticleSystems* m_particles = nullptr;
    std::function<std::optional<camera::CameraView>()> m_view;
    std::function<void(double, double)> m_damage;            // setDamageReceiver()
    std::function<void(double, double, int, bool)> m_carHit; // setCarHitReceiver()
    LevelCrimeServices m_crimes;
    GameState* m_state = nullptr;
    CreatedHumans* m_humans = nullptr;

    // The 0-based player whose human is `human`; -1 for none.
    [[nodiscard]] int playerOf(double human) const;
    world_objects::Cars* m_cars = nullptr;
    world_objects::SpawnRecords* m_records = nullptr; // setSpawnRecords()
};

} // namespace coney
