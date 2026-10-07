// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/cars.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

#include "core/name_hash.h"
#include "effects/particles.h"

namespace coney::world_objects {

namespace {

// The highest part id: parts are 0-25.
constexpr std::uint32_t kLastPart = kCarParts - 1;

// One component 0-1 as a byte: × 255, kept to 0-255. **Coney's choice**: truncated, as the page does not say whether
// `0x0017aca8` rounds.
std::uint32_t componentByte(float component) {
    const float scaled = std::clamp(component, 0.0F, 1.0F) * 255.0F;
    return static_cast<std::uint32_t>(scaled);
}

// A window's burst direction in world axes (`0x0038a830`): along the car's −x for the left windows 15 and 19, +x for
// 17 and 21; zero for any other part. **Coney's reading**: the part frame's x taken as the car's x (the windows'
// frames are not turned on the page).
anim::Vec3 windowBurst(const Car& car, std::uint32_t part) {
    float side = 0.0F;
    if (part == 15 || part == 19) {
        side = -1.0F;
    } else if (part == 17 || part == 21) {
        side = 1.0F;
    }
    if (side == 0.0F) {
        return anim::Vec3{};
    }
    return anim::transformDirection(anim::matrixFromQuat(anim::normalise(car.rotation)), anim::Vec3{side, 0.0F, 0.0F});
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
    if (Car* car = find(handle); car != nullptr) {
        car->removedParts = 0;
        car->openParts = 0;
        car->damagedParts = 0;
        car->dirty = true;
    }
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
    const auto dollars = static_cast<std::uint8_t>(itemKind & 0xffU);
    if (itemKind != 0) {
        car->trunkObject = 0;
        car->trunkMoney = dollars;
    } else {
        // The object is pinned, so streaming never stores it while it waits.
        if (m_records != nullptr) {
            m_records->setPinned(object, true);
        }
        car->trunkObject = object;
        car->trunkMoney = 0;
    }
    car->trunkLoaded = true;
}

bool Cars::damagePart(double handle, std::uint32_t part, float amount, bool instant) {
    Car* car = find(handle);
    if (car == nullptr || part >= car->damage.size()) {
        return false;
    }
    const std::uint32_t bit = 1U << part;
    if ((car->removedKept & bit) != 0) {
        return false;
    }
    float& damage = car->damage.at(part);
    damage = instant || (kCarWindowParts & bit) != 0 ? 1.0F : damage + amount;
    if (damage < 1.0F) {
        return false;
    }
    car->removedKept |= bit;
    car->dirty = true;
    m_breaks.push_back(CarPartBreak{.car = handle,
                                    .part = part,
                                    .window = (kCarWindowParts & bit) != 0,
                                    .instant = instant,
                                    .burst = windowBurst(*car, part),
                                    .carPosition = car->position,
                                    .carRotation = car->rotation});
    if (part == kBootPart && !instant && car->trunkLoaded) {
        car->trunkLoaded = false;
        releaseTrunk(*car);
    }
    // The window beside the stereo frees it for a theft (a kind-3 context record).
    if (part == kStereoWindowPart && car->stereo == StereoState::InCar) {
        car->stereo = StereoState::Freed;
    }
    return true;
}

bool Cars::explode(double handle) {
    Car* car = find(handle);
    if (car == nullptr || car->exploded) {
        return false;
    }
    for (std::uint32_t part = 0; part < kCarParts; ++part) {
        static_cast<void>(damagePart(handle, part, 1.0F, true));
    }
    car->exploded = true;
    return true;
}

CarPartMask Cars::humanHit(double handle, anim::Vec3 standing, std::vector<CarHitReport>* reports) {
    const Car* car = find(handle);
    if (car == nullptr) {
        return 0;
    }
    // Parts 1-25: the ones message 0x19 names (the body, part 0, is never reported).
    constexpr std::uint32_t kReported = ((1U << kCarParts) - 1U) & ~1U;
    const bool allOffBefore = (car->removedKept & kReported) == kReported;
    const CarPartMask struck = carHumanHitParts(*car, standing);
    for (std::uint32_t part = 0; part <= kLastPart; ++part) {
        if ((struck & (1U << part)) == 0) {
            continue;
        }
        const bool wasOff = (car->removedKept & (1U << part)) != 0;
        const bool broke = damagePart(handle, part, kHumanCarHitDamage, false);
        if (reports != nullptr && part >= 1 && !wasOff) {
            reports->push_back(CarHitReport{.part = static_cast<int>(part), .broke = broke});
        }
    }
    if (reports != nullptr && !allOffBefore && (car->removedKept & kReported) == kReported) {
        reports->push_back(CarHitReport{.part = kCarHitAllBroken, .broke = true});
    }
    return struck;
}

anim::Vec3 Cars::bootPosition(const Car& car) { return carBootPosition(car); }

void Cars::releaseTrunk(Car& car) {
    if (m_records == nullptr) {
        return;
    }
    const anim::Vec3 at = bootPosition(car);
    const std::array<float, 3> position{at.x, at.y, at.z};
    if (car.trunkObject != 0) {
        if (SpawnRecord* record = m_records->find(car.trunkObject); record != nullptr) {
            record->position = position;
        }
        return;
    }
    if (car.trunkMoney != 0 && m_nextHandle) {
        static_cast<void>(m_records->add(SpawnRecord{.handle = m_nextHandle(),
                                                     .typeName = std::string(kMoneyPickup),
                                                     .position = position,
                                                     .money = car.trunkMoney}));
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
