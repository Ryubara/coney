// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/cars.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <string>
#include <utility>

#include "core/name_hash.h"
#include "effects/particles.h"

namespace coney::world_objects {

namespace {

// The highest part id with a part record: parts are 0-25.
constexpr std::uint32_t kLastPart = kCarParts - 1;
// The highest hit bit: bits 26-29 are the lamps' glass, which has no part record (docs/research/cars.md#type-tables).
constexpr std::uint32_t kLastHitBit = 29;
// The roof's part id: it coming off ends the police car's lights.
constexpr std::uint32_t kRoofPart = 1;

// The part record's byte +0x01, how a part comes off (docs/research/cars.md#type-tables).
constexpr std::uint8_t kOffAtFirstDamage = 0x10; // any damage takes it off at once
constexpr std::uint8_t kThrownByHit = 0x02;      // a knock throws it
constexpr std::uint8_t kThrownByBlast = 0x04;    // a blast throws it (a quiet one hides it)
// That byte for parts 0-25, the same in every type's record but the van's (below).
constexpr std::array<std::uint8_t, kCarParts> kPartComesOff{0x00, 0x18, 0x02, 0x02, 0x07, 0x06, 0x18, 0x18, 0x18,
                                                            0x18, 0x02, 0x02, 0x02, 0x02, 0x46, 0x18, 0x26, 0x18,
                                                            0x46, 0x18, 0x26, 0x18, 0x04, 0x04, 0x04, 0x04};
// The van's boot and side panels 12 and 13 come off at the first damage and never fly.
constexpr std::uint8_t kVanType = 4;
constexpr std::uint8_t kVanPanel = 0x18;

// How part `part` of `car` comes off: its record's byte +0x01. A car of no known type reads the sedan's.
std::uint8_t comesOff(const Car& car, std::uint32_t part) {
    if (car.type == kVanType && (part == kBootPart || part == 12 || part == 13)) {
        return kVanPanel;
    }
    return kPartComesOff.at(part);
}

// The parts message 0x19's all-broken call waits for (docs/research/cars.md#type-tables): 1-21, the coupe's 1-17,
// the van's 1-21 but 7.
CarPartMask allBrokenParts(const Car& car) {
    constexpr std::uint8_t kCoupe = 1;
    if (car.type == kCoupe) {
        return 0x3fffeU;
    }
    return car.type == kVanType ? 0x3fff7eU : 0x3ffffeU;
}
// The parts whose damage checks it: 1-21.
constexpr CarPartMask kAllBrokenChecked = 0x3ffffeU;

// The parts a hit or a blast passes over: removed (`+0x11f0`) or off (`+0x11f8`). **Coney's stand-in**: the flying
// parts (`+0x11f4`) are not modelled (#72), so none is skipped for flying.
CarPartMask passedOver(const Car& car) { return car.removedParts | car.removedKept; }
// A part's damaged form is the atomic 25 after it (docs/research/cars.md#hit-effects).
constexpr std::uint32_t kDamagedAtomicOffset = 25;

// One component 0-1 as a byte: × 255, kept to 0-255. **Coney's choice**: truncated, as the page does not say whether
// `0x0017aca8` rounds.
std::uint32_t componentByte(float component) {
    const float scaled = std::clamp(component, 0.0F, 1.0F) * 255.0F;
    return static_cast<std::uint32_t>(scaled);
}

} // namespace

std::uint32_t packCarColour(const std::array<float, 4>& components) {
    return componentByte(components[0]) | (componentByte(components[1]) << 8U) | (componentByte(components[2]) << 16U) |
           (componentByte(components[3]) << 24U);
}

CarPaint paintOf(std::uint32_t word) {
    // Bytes reversed: the last component (the word's top byte) is red, the first alpha.
    return CarPaint{static_cast<std::uint8_t>(word >> 24U), static_cast<std::uint8_t>(word >> 16U),
                    static_cast<std::uint8_t>(word >> 8U), static_cast<std::uint8_t>(word)};
}

std::optional<std::uint32_t> linkedCarPart(std::uint32_t part) {
    switch (part) {
    case 14:
    case 16:
    case 18:
    case 20:
        return part + 1;
    default:
        return std::nullopt;
    }
}

Car* Cars::spawn(std::string_view typeName, anim::Vec3 position, anim::Quat rotation, double handle) {
    if (m_cars.size() >= kPool) {
        return nullptr;
    }
    Car& car = m_cars.emplace_back();
    car.handle = handle;
    car.position = position;
    car.rotation = rotation;
    // The type is the first of the six names that matches (strcmp, so exact).
    if (const auto found = std::ranges::find(kCarTypeNames, typeName); found != kCarTypeNames.end()) {
        car.type = static_cast<std::uint8_t>(found - kCarTypeNames.begin());
    }
    car.nameHash = crc32(typeName);
    // The police car brings its lights, attached to it.
    if (car.type == kPoliceCarType && m_particles != nullptr) {
        car.lights = m_particles->spawn("part_copcar_lights", position, rotation, handle) != nullptr;
    }
    return &car;
}

Car* Cars::find(double handle) {
    const auto found = std::ranges::find(m_cars, handle, &Car::handle);
    return handle != 0 && found != m_cars.end() ? &*found : nullptr;
}

const Car* Cars::find(double handle) const {
    const auto found = std::ranges::find(m_cars, handle, &Car::handle);
    return handle != 0 && found != m_cars.end() ? &*found : nullptr;
}

bool Cars::destroy(double handle) {
    const auto found = std::ranges::find(m_cars, handle, &Car::handle);
    if (handle == 0 || found == m_cars.end()) {
        return false;
    }
    if (found->lights && m_particles != nullptr) {
        m_particles->killFollowing(handle);
    }
    m_cars.erase(found);
    return true;
}

void Cars::setColour(double handle, const std::array<float, 4>& components) {
    if (Car* car = find(handle); car != nullptr) {
        const std::uint32_t word = packCarColour(components);
        car->paint = {word, word};
        car->painted = true;
        car->dirty = true;
    }
}

void Cars::repair(double handle) {
    Car* car = find(handle);
    if (car == nullptr) {
        return;
    }
    // Car_ResetParts (0x003875e8): every part record back to new (no damage, shut, its form undamaged) and every mask
    // cleared, the exploded flag too. **Coney's stand-in**: the rock (`+0x12d6`) and the loose pieces a record ends
    // are not modelled (#72).
    car->damage.fill(0.0F);
    car->openParts = 0;
    car->damagedParts = 0;
    car->firstHits = 0;
    car->removedParts = 0;
    car->removedKept = 0;
    car->exploded = false;
}

void Cars::removePart(double handle, std::uint32_t part, bool on) {
    Car* car = find(handle);
    if (car == nullptr || part > kLastPart) {
        return;
    }
    const std::uint32_t bit = 1U << part;
    if (!on) {
        car->removedParts &= ~bit;
    } else {
        car->removedParts |= bit;
        car->removedKept |= bit;
        if (const std::optional<std::uint32_t> linked = linkedCarPart(part)) {
            car->removedParts |= 1U << *linked;
        }
    }
    car->dirty = true;
}

void Cars::placeInTrunk(double handle, double object, std::uint32_t itemKind) {
    Car* car = find(handle);
    if (car == nullptr) {
        return;
    }
    // Dollars (Car_SetTrunkItemKind): only the amount and the loaded flag; an object put in before stays, and the
    // release prefers it.
    if (itemKind != 0) {
        car->trunkMoney = static_cast<std::uint8_t>(itemKind & 0xffU);
        car->trunkLoaded = true;
        return;
    }
    // An object goes in only when its handle resolves.
    if (m_records == nullptr || m_records->resolve(object) == nullptr) {
        return;
    }
    // Car_SetTrunkObject: pinned, so streaming never stores it while it waits; the dollars stay.
    m_records->setPinned(object, true);
    car->trunkObject = object;
    car->trunkLoaded = true;
}

bool Cars::damagePart(double handle, std::uint32_t part, float amount, bool instant, bool quiet) {
    Car* car = find(handle);
    if (car == nullptr || part > kLastPart) {
        return false;
    }
    const std::uint32_t bit = 1U << part;
    // The callers' skip (Car_OnHit, Car_DoExplode): a part removed or off takes nothing. **Coney's stand-in**: here
    // for every caller, so a door's window only removed (`+0x11f0`) is skipped too, which the original damages.
    if ((passedOver(*car) & bit) != 0) {
        return false;
    }
    // The damage adds up; an instant call, or a part whose record says so, takes it to 1 at once.
    const std::uint8_t how = comesOff(*car, part);
    float& damage = car->damage.at(part);
    damage = instant || (how & kOffAtFirstDamage) != 0 ? 1.0F : damage + amount;
    if (damage < 1.0F) {
        return false;
    }
    damage = 1.0F;
    car->removedKept |= bit;
    // A door knocked off (not blown off) takes its window with it, at full damage, unless that is off already.
    if (const std::optional<std::uint32_t> window = linkedCarPart(part);
        !instant && window && (car->removedKept & (1U << *window)) == 0) {
        static_cast<void>(damagePart(handle, *window, 1.0F, false));
    }
    // The window beside the stereo frees it for a theft (a kind-3 context record).
    if (part == kStereoWindowPart && car->stereo == StereoState::InCar) {
        car->stereo = StereoState::Freed;
    }
    // The roof ends the police car's lights (message 0x15, then the system ends).
    if (part == kRoofPart && car->lights) {
        if (m_particles != nullptr) {
            m_particles->killFollowing(handle);
        }
        car->lights = false;
    }
    // The boot knocked off releases a loaded boot's item; blown off, the item is lost.
    if (part == kBootPart && !instant && car->trunkLoaded) {
        car->trunkLoaded = false;
        releaseTrunk(*car);
    }
    // A part its record throws: a quiet blast hides it instead. **Coney's stand-in**: the thrown piece
    // (Car_DetachPart and the car's rescheduling) is not ported (#72); the part keeps its damaged form in place.
    const std::uint8_t thrownBy = instant ? kThrownByBlast : kThrownByHit;
    if ((how & thrownBy) != 0 && quiet) {
        car->removedParts |= bit;
    }
    return true;
}

bool Cars::explode(double handle, bool quiet) {
    Car* car = find(handle);
    // Car_TryExplode: once only. **Coney's stand-in**: the original also refuses while a molotov's countdown
    // (`+0x1200`) runs, which is not ported (#72).
    if (car == nullptr || car->exploded) {
        return false;
    }
    // Every hit bit 1-29 not passed over: a part takes instant damage, a lamp's glass is removed and off; each is
    // marked hit. **Coney's stand-in**: the loud blast's fireball, shake, sound and lift are not ported (#72).
    for (std::uint32_t part = 1; part <= kLastHitBit; ++part) {
        const std::uint32_t bit = 1U << part;
        if ((passedOver(*car) & bit) != 0) {
            continue;
        }
        if (part <= kLastPart) {
            static_cast<void>(damagePart(handle, part, 1.0F, true, quiet));
        } else {
            car->removedParts |= bit;
            car->removedKept |= bit;
        }
        car->firstHits |= bit;
    }
    // **Coney's stand-in**: the loud blast's statistic, heat wave, AI alert, 300 damage, broken glass, the wreck's
    // lower box and the flags switched off near it are not ported (#72); its message 0x19 is the binding's.
    car->exploded = true;
    // The stereo, in the car or freed, is destroyed with it.
    if (car->stereo == StereoState::InCar || car->stereo == StereoState::Freed) {
        car->stereo = StereoState::Destroyed;
    }
    return true;
}

CarPartMask Cars::humanHit(double handle, anim::Vec3 standing, anim::Vec3 point, std::vector<CarHitReport>* reports,
                           bool player) {
    Car* car = find(handle);
    if (car == nullptr) {
        return 0;
    }
    const CarPartMask struck = carHumanHitParts(*car, standing, player);
    // Any part outside the glass: sparks and dust at the hit point. **Coney's stand-in**: only for a part not off yet,
    // as carHumanHitParts() leaves the others out; the original sparks for any (#72).
    if (const CarPartMask solid = struck & ~kCarGlassParts; solid != 0) {
        m_effects.push_back(CarHitEffect{.car = handle,
                                         .part = static_cast<std::uint32_t>(std::countr_zero(solid)),
                                         .kind = CarHitEffectKind::Sparks,
                                         .position = point,
                                         .direction = anim::Vec3{0.0F, 1.0F, 0.0F},
                                         .carPosition = car->position,
                                         .carRotation = car->rotation});
    }
    // Each hit bit 1-29 struck and not passed over (a door's window may have gone with it): a part takes the damage, a
    // lamp's glass is removed and off; then its first hit ever sends its effect.
    for (std::uint32_t part = 1; part <= kLastHitBit; ++part) {
        const std::uint32_t bit = 1U << part;
        if ((struck & bit) == 0 || (passedOver(*car) & bit) != 0) {
            continue;
        }
        if (part <= kLastPart) {
            // **Coney's stand-in**: the dent sound of a panel or door crossing 0.5 is not ported (#72).
            const bool broke = damagePart(handle, part, kHumanCarHitDamage, false);
            // Message 0x19 for the part, then, after a part 1-21, the all-broken call once the type's parts are off.
            if (reports != nullptr) {
                reports->push_back(CarHitReport{.part = static_cast<int>(part), .broke = broke});
                const CarPartMask all = allBrokenParts(*car);
                if ((kAllBrokenChecked & bit) != 0 && (car->removedKept & all) == all) {
                    reports->push_back(CarHitReport{.part = kCarHitAllBroken, .broke = true});
                }
            }
        } else {
            car->removedParts |= bit;
            car->removedKept |= bit;
        }
        if ((car->firstHits & bit) == 0) {
            car->firstHits |= bit;
            m_effects.push_back(carBreakWindowEffect(*car, part, point));
        }
    }
    // **Coney's stand-in**: every part struck is returned; the original returns those outside the glass, which its
    // caller sounds (#72).
    return struck;
}

CarHitEffect carBreakWindowEffect(const Car& car, std::uint32_t part, anim::Vec3 point) {
    // The bonnet's steam point in the car's frame (docs/research/cars.md#hit-effects).
    constexpr anim::Vec3 kSteamPoint{0.0F, 2.741F, 0.023F};
    CarHitEffect effect{.car = car.handle,
                        .part = part,
                        .kind = CarHitEffectKind::Sparks,
                        .position = point,
                        .direction = anim::Vec3{0.0F, 1.0F, 0.0F},
                        .carPosition = car.position,
                        .carRotation = car.rotation};
    // The glass's effect sits on the part's damaged atomic, along a row of its frame.
    const auto atAtomic = [&](CarHitEffectKind kind, int row, float sign) {
        effect.kind = kind;
        effect.fromAtomic = true;
        effect.atomic = part + kDamagedAtomicOffset;
        effect.frameRow = row;
        effect.rowSign = sign;
    };
    switch (part) {
    case 4: {
        const anim::Mat34 turn = anim::matrixFromQuat(anim::normalise(car.rotation));
        effect.kind = CarHitEffectKind::Steam;
        effect.position = anim::add(car.position, anim::transformDirection(turn, kSteamPoint));
        effect.direction = anim::Vec3{0.0F, 0.0F, 1.0F};
        break;
    }
    case 6:
        atAtomic(CarHitEffectKind::LargeShatter, 1, 1.0F);
        break;
    case 7:
        atAtomic(CarHitEffectKind::LargeShatter, 1, -1.0F);
        break;
    case 15:
    case 19:
        atAtomic(CarHitEffectKind::SmallShatter, 0, -1.0F);
        break;
    case 17:
    case 21:
        atAtomic(CarHitEffectKind::SmallShatter, 0, 1.0F);
        break;
    case 26:
    case 27:
        effect.kind = CarHitEffectKind::HeadlightGlass;
        break;
    case 28:
    case 29:
        effect.kind = CarHitEffectKind::RearLightGlass;
        break;
    default:
        break;
    }
    return effect;
}

anim::Vec3 Cars::bootPosition(const Car& car) { return carBootPosition(car); }

void Cars::releaseTrunk(Car& car) {
    car.trunkLoaded = false;
    if (m_records == nullptr) {
        return;
    }
    const anim::Vec3 at = bootPosition(car);
    const std::array<float, 3> position{at.x, at.y, at.z};
    // No object: the dollars become a pickup at the boot, and are spent. **Coney's stand-in**: the pickup's 15 s
    // respawn timer is not applied (Coney's world objects keep no timers yet, as level_pickups.h says).
    if (car.trunkObject == 0) {
        if (car.trunkMoney != 0 && m_nextHandle) {
            const SpawnRecord* pickup = m_records->add(SpawnRecord{.handle = m_nextHandle(),
                                                                   .typeName = std::string(kMoneyPickup),
                                                                   .position = position,
                                                                   .shownMessage = std::nullopt,
                                                                   .money = car.trunkMoney,
                                                                   .model = std::nullopt,
                                                                   .physicsBody = std::nullopt,
                                                                   .breakIn = std::nullopt});
            if (pickup != nullptr) {
                car.trunkMoney = 0;
            }
        }
        return;
    }
    // The object moves to the boot; one that no longer resolves is forgotten. **Coney's stand-in**: its body's wake
    // and update interval are not kept (Coney's objects have no bodies, SpawnRecord::physicsBody).
    if (SpawnRecord* record = m_records->resolve(car.trunkObject); record != nullptr) {
        record->position = position;
    } else {
        car.trunkObject = 0;
    }
}

void Cars::spawnRadio(double handle) {
    if (Car* car = find(handle); car != nullptr && car->stereo == StereoState::None) {
        car->stereo = StereoState::InCar;
    }
}

bool Cars::freeStereo(double handle) {
    Car* car = find(handle);
    if (car == nullptr || car->stereo != StereoState::InCar) {
        return false;
    }
    car->stereo = StereoState::Freed;
    return true;
}

bool Cars::takeStereo(double handle) {
    Car* car = find(handle);
    if (car == nullptr || car->stereo != StereoState::Freed) {
        return false;
    }
    car->stereo = StereoState::Taken;
    return true;
}

anim::Vec3 Cars::stereoPosition(const Car& car) { return carStereoPosition(car); }

std::vector<StereoDraw> stereoDraws(const Cars& cars) {
    static const std::uint32_t modelHash = crc32(kCarStereoType);
    std::vector<StereoDraw> draws;
    for (const Car& car : cars.all()) {
        if (car.stereo == StereoState::InCar || car.stereo == StereoState::Freed) {
            draws.push_back(StereoDraw{.handle = car.handle + 0.25,
                                       .modelHash = modelHash,
                                       .position = Cars::stereoPosition(car),
                                       .rotation = car.rotation});
        }
    }
    return draws;
}

} // namespace coney::world_objects
