// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/object_breaks.h"

namespace coney::world_objects {

namespace {

// Seconds a tick lasts.
constexpr float kTick = 1.0F / 60.0F;

// A record's position as our vector.
anim::Vec3 positionOf(const SpawnRecord& record) {
    return anim::Vec3{record.position[0], record.position[1], record.position[2]};
}

} // namespace

std::size_t sendDestroyInRadius(SpawnRecords& records, anim::Vec3 centre, float radius, double centreObject) {
    std::size_t told = 0;
    for (const SpawnRecord& found : records.all()) {
        const bool inManager = found.live || found.handle == centreObject;
        if (found.removed || !inManager || anim::distance(positionOf(found), centre) > radius) {
            continue;
        }
        ++told;
        // A molotov already counting down keeps its time.
        SpawnRecord* record = records.find(found.handle);
        if (record != nullptr && record->typeName == kMolotovType && !record->breakIn) {
            record->breakIn = kMolotovBreakTicks * kTick;
        }
    }
    return told;
}

std::vector<anim::Vec3> stepSelfBreaks(SpawnRecords& records, float seconds) {
    std::vector<anim::Vec3> flashes;
    std::vector<double> broken;
    for (const SpawnRecord& found : records.all()) {
        if (found.removed || !found.breakIn) {
            continue;
        }
        SpawnRecord* record = records.find(found.handle);
        std::optional<float>& breakIn = record->breakIn; // the same record as found, so set
        if (!breakIn) {
            continue;
        }
        *breakIn -= seconds;
        // A little slack, so a whole number of steps reaches the tick count despite rounding.
        if (*breakIn <= 1.0e-4F) {
            const anim::Vec3 at = positionOf(*record);
            flashes.push_back(anim::Vec3{at.x, at.y, at.z + kMolotovFlashRise});
            broken.push_back(record->handle);
        }
    }
    // The bottle ends with its break.
    for (const double handle : broken) {
        static_cast<void>(records.destroy(handle));
    }
    return flashes;
}

} // namespace coney::world_objects
