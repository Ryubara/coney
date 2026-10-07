// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "world_objects/car_hits.h"
#include "world_objects/car_types.h"
#include "world_objects/spawn_records.h"

namespace coney::effects {
class ParticleSystems;
} // namespace coney::effects

namespace coney::world_objects {

/// The boot's part id (docs/research/cars.md#parts).
inline constexpr std::uint32_t kBootPart = 5;
/// The pickup a boot's dollars become (type 0x1c: picking it up gives a player that much money).
inline constexpr std::string_view kMoneyPickup = "dyn_money";

/// The type index of the police car (`car_copcar`), which brings its lights.
inline constexpr std::uint8_t kPoliceCarType = 3;

/// A car's stealable stereo (`CarSpawnRadio`): in the car until a broken window frees it, then taken by a theft
/// (docs/research/crimes.md#stereo).
enum class StereoState : std::uint8_t {
    None,  ///< The car has none.
    InCar, ///< Put in by `CarSpawnRadio`, behind the window.
    Freed, ///< Its window broken: a theft (kind-3 record) can take it.
    Taken, ///< Stolen.
};

/// One parked car: the fields of the car task Coney keeps (docs/research/cars.md#car-object).
struct Car {
    double handle = 0;                ///< Its script handle.
    std::optional<std::uint8_t> type; ///< `+0x11e0`: index into kCarTypeNames; unset for an unknown name.
    std::uint32_t nameHash = 0;       ///< `+0x12f0`: CRC-32 of the type name.
    anim::Vec3 position;              ///< Where it stands (game axes).
    anim::Quat rotation;              ///< Its turn.
    std::array<std::uint32_t, 2> paint{0xFFFFFFFFU, 0xFFFFFFFFU}; ///< `+0x12e8`, `+0x12ec`: `CarSetColor`'s word.
    bool painted = false;                   ///< Whether `CarSetColor` has run (the model's own colour before).
    std::uint32_t removedParts = 0;         ///< `+0x11f0`: one bit per part.
    std::uint32_t removedKept = 0;          ///< `+0x11f8`: the same bits, kept.
    std::uint32_t openParts = 0;            ///< Bit 2 of each part's state byte, one bit per part.
    std::uint32_t damagedParts = 0;         ///< Parts showing their damaged form (atomic `p + 25`).
    bool dirty = true;                      ///< `+0x1308`: the model needs its colour and transform again.
    bool lights = false;                    ///< `+0x1210`: whether it made the police car's `part_copcar_lights`.
    StereoState stereo = StereoState::None; ///< `CarSpawnRadio`'s stereo.
    std::array<float, kCarParts> damage{};  ///< `+0x230 + part × 0xa0`: each part's damage; at 1 it comes off.
    bool trunkLoaded = false;               ///< `+0x12e4`: something waits in the boot.
    double trunkObject = 0;                 ///< `+0x120c`: the object in the boot, 0 for none.
    std::uint8_t trunkMoney = 0;            ///< `+0x12e5`: else the dollars a `dyn_money` pickup will hold.
    bool exploded = false;                  ///< `+0x12d5`: it has blown up.
};

/// What `Car_UpdateRender` makes of a paint word for the model: `CarSetColor`'s `{c1, c2, c3, c4}` stored in that
/// order, read back reversed, so c4 is red and c1 alpha (docs/research/cars.md#colour).
struct CarPaint {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;

    friend bool operator==(const CarPaint&, const CarPaint&) = default;
};

/// The paint word of `CarSetColor(car, {c1, c2, c3, c4})`: each component 0-1 made a byte (× 255), stored in the order
/// given (c1 the low byte).
/// @orig 0x0038df38 Car_SetColour (unknown)
[[nodiscard]] std::uint32_t packCarColour(const std::array<float, 4>& components);

/// The colour a paint word draws in: its bytes reversed (`0x00338240`).
[[nodiscard]] CarPaint paintOf(std::uint32_t word);

/// What one human hit did to a car, as the car's message 0x19 reports it (docs/research/cars.md): a part 1-25 damaged
/// (`part`, `broke` when this hit took it off), or kCarHitAllBroken with `broke` true for the hit that left every part
/// off.
struct CarHitReport {
    int part = 0;
    bool broke = false;
};
/// CarHitReport::part for the hit that leaves every part of a car off.
inline constexpr int kCarHitAllBroken = -2;

/// The parked cars: `CarSpawn` makes one from the car pool (object manager `+0x844`, 18 cars) and the car bindings
/// change it by its handle. Coney keeps the cars' state; the platform draws them with their models and makes them
/// obstacles (src/platform/parked_cars.h).
///
/// **Coney's stand-ins** where the page is silent: a name that matches no type still makes a car, which is never drawn
/// (the original leaves its index unset); a car stands as placed (how cars move, `0x0038e590`, is not traced).
///
/// Research: docs/research/cars.md, docs/research/crimes.md#stereo
class Cars {
  public:
    /// Cars in the pool (docs/research/tasks.md#classes).
    static constexpr std::size_t kPool = 18;

    /// Makes the police car's lights in `particles` (null: none).
    void setParticles(effects::ParticleSystems* particles) { m_particles = particles; }

    /// `CarSpawn`: a car of type `typeName` at `position` turned by `rotation`, named by `handle`. Null when the pool
    /// is full.
    /// @orig 0x0038dde8 Car_Spawn (unknown)
    /// @orig 0x00387bc8 Car_Init (unknown)
    Car* spawn(std::string_view typeName, anim::Vec3 position, anim::Quat rotation, double handle);

    /// The car `handle` names; null when none does.
    [[nodiscard]] Car* find(double handle);
    [[nodiscard]] const Car* find(double handle) const;

    /// `CarDestroy`: the car leaves the world at once, with no effect: its lights (a particle system of its handle)
    /// go with it, and the car goes back to the pool. Returns whether the handle named a car.
    /// @orig 0x0038dea8 Car_Destroy (unknown)
    /// @orig 0x00387ad8 Car_Release (unknown)
    bool destroy(double handle);

    /// `CarSetColor`: the car's paint (packCarColour()) in both copies, the car marked dirty. An unknown handle is
    /// ignored.
    /// @orig 0x0038df38 Car_SetColour (unknown)
    void setColour(double handle, const std::array<float, 4>& components);

    /// `CarMakeGoodAsNew`: every part reset (none removed, open or damaged) and the car marked dirty. A handle that is
    /// not a car does nothing.
    /// @orig 0x0038e068 Car_Repair (unknown)
    void repair(double handle);

    /// `CarRemovePart(car, part, on)`'s bits (docs/research/cars.md#parts): with `on`, `part` and its linked part
    /// (a door's window) removed; without, `part`'s bit cleared.
    /// @orig 0x0038c7d8 Car_RemovePartBits (unknown)
    void removePart(double handle, std::uint32_t part, bool on);

    /// What a boot's release makes: a `dyn_money` pickup's spawn record (Cars::setObjects()).
    using NextHandle = std::function<double()>;
    /// Sets the spawn records a boot's item is moved in or added to, and where a new record's handle comes from (null:
    /// a release changes no record).
    void setObjects(SpawnRecords* records, NextHandle nextHandle) {
        m_records = records;
        m_nextHandle = std::move(nextHandle);
    }

    /// `CarPlaceInTrunkOnDetach(car, object, itemKind)`: with `itemKind` 0 the object is pinned and put in the boot,
    /// otherwise the low 8 bits of `itemKind` are dollars for a `dyn_money` pickup; the boot is marked loaded. A later
    /// call replaces the earlier item; an unknown car does nothing.
    /// @orig 0x0038e0f0 Car_PlaceInTrunkOnDetach (unknown)
    /// @orig 0x0038d538 Car_SetTrunkObject (unknown)
    /// @orig 0x0038d528 Car_SetTrunkItemKind (unknown)
    void placeInTrunk(double handle, double object, std::uint32_t itemKind);
    /// A part takes `amount` of damage (`instant`, or a window: set to 1 at once); at 1 it comes off (its bit in the
    /// kept removed parts, `+0x11f8`). The boot (part 5) coming off a non-instant call releases a loaded boot's item;
    /// an instant one (the car exploding) loses it; window 15 coming off frees the stereo. A part already off takes
    /// nothing. Returns whether the part came off now.
    /// @orig 0x0038a4d8 Car_DamagePart (unknown)
    bool damagePart(double handle, std::uint32_t part, float amount, bool instant);
    /// `CarExplode`'s wreck: a car not yet exploded has every part not already off knocked off (instant damage, so a
    /// loaded boot's item is lost) and is marked exploded. Returns whether it exploded now (false for an unknown car or
    /// one exploded already).
    /// @orig 0x0038ab18 Car_TryExplode (unknown)
    /// @orig 0x0038ab50 Car_DoExplode (unknown)
    bool explode(double handle);
    /// A plain human's strike from `standing` reached the car: each part carHumanHitParts() names takes
    /// kHumanCarHitDamage (a window breaks at once). Returns the parts struck; `reports` (when given) gets what message
    /// 0x19 says of the hit: each part 1-25 not already off that it damaged, then kCarHitAllBroken if it left every
    /// part 1-25 off. **Coney's reading**: the gang lock
    /// (`+0x12d8`), the exploding car, message `0x19`, the effects and the statistic are not modelled yet.
    /// @orig 0x0038bea0 Car_OnHit (unknown)
    CarPartMask humanHit(double handle, anim::Vec3 standing, std::vector<CarHitReport>* reports = nullptr);
    /// Where a car's boot item is released (game axes): carBootPosition().
    [[nodiscard]] static anim::Vec3 bootPosition(const Car& car);

    /// `CarSpawnRadio`: puts a stereo in the car. An unknown handle is ignored.
    /// @orig 0x0038d690 Car_SpawnRadio (unknown)
    void spawnRadio(double handle);
    /// The car's window broke: its stereo, if in the car, is freed for a theft. Returns whether one was.
    bool freeStereo(double handle);
    /// A theft took the car's freed stereo. Returns whether there was one to take.
    bool takeStereo(double handle);
    /// Where a car's stereo sits (game axes): carStereoPosition().
    [[nodiscard]] static anim::Vec3 stereoPosition(const Car& car);

    /// Every car, oldest first.
    [[nodiscard]] const std::vector<Car>& all() const { return m_cars; }
    /// Forgets every car: the level is unloaded.
    void clear() { m_cars.clear(); }

  private:
    // `Car_ReleaseTrunkItem`: the boot's object moved to the boot, or a `dyn_money` pickup of its dollars made there.
    // @orig 0x0038d188 Car_ReleaseTrunkItem (unknown)
    void releaseTrunk(Car& car);

    std::vector<Car> m_cars;
    effects::ParticleSystems* m_particles = nullptr;
    SpawnRecords* m_records = nullptr;
    NextHandle m_nextHandle;
};

/// The part removed with `part` (a door's window), or nothing: doors 14, 16, 18, 20 take windows 15, 17, 19, 21
/// (docs/research/cars.md#type-record; the same in every type, inferred).
[[nodiscard]] std::optional<std::uint32_t> linkedCarPart(std::uint32_t part);

} // namespace coney::world_objects
