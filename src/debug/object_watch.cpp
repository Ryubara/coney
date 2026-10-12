// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/object_watch.h"

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace coney::debug {

namespace {

// "yes" or "no".
std::string yesNo(bool on) { return on ? "yes" : "no"; }

// A door class's short name.
std::string_view doorClassName(world_objects::DoorClass doorClass) {
    switch (doorClass) {
    case world_objects::DoorClass::Swinging:
        return "swinging";
    case world_objects::DoorClass::Barrier:
        return "barrier";
    case world_objects::DoorClass::Other:
        return "other";
    }
    return "?";
}

// A swinging door's state byte by name (world_objects::door_state), with the number.
std::string doorStateText(int state) {
    namespace s = world_objects::door_state;
    std::string_view name = "?";
    if (state == s::kClosing || state == s::kClosing + 1) {
        name = "closing";
    } else if (state == s::kClosed) {
        name = "closed";
    } else if (state == s::kOpening) {
        name = "opening";
    } else if (state == s::kSwung) {
        name = "swung";
    } else if (state == s::kOpen) {
        name = "open";
    } else if (state == s::kBroken) {
        name = "broken";
    } else if (state == s::kCabinRest) {
        name = "cabin rest";
    }
    return std::format("{} ({})", name, state);
}

// A quaternion as the inspector prints it: "(x, y, z, w)".
std::string quatText(float x, float y, float z, float w) {
    return std::format("({:.3f}, {:.3f}, {:.3f}, {:.3f})", x, y, z, w);
}

// A group being filled line by line.
struct Lines {
    WatchGroup group;
    // Appends the line `name: value`.
    void add(std::string name, std::string value) {
        group.lines.push_back(WatchLine{.name = std::move(name), .value = std::move(value)});
    }
};

// The Identity group every object starts with.
WatchGroup identity(EntityId id, std::string_view kind, double handle, const std::string& type) {
    Lines lines{.group = WatchGroup{.name = "Identity", .lines = {}}};
    lines.add("Entity", std::format("{}", id));
    lines.add("Kind", std::string(kind));
    lines.add("Handle", std::format("{}", handle));
    lines.add("Type", type.empty() ? std::string("-") : type);
    return std::move(lines.group);
}

} // namespace

std::vector<WatchGroup> describeDoor(const world_objects::Door& door, EntityId id) {
    std::vector<WatchGroup> groups;
    groups.push_back(identity(id, "door", door.handle, door.type));
    Lines transform{.group = WatchGroup{.name = "Transform", .lines = {}}};
    transform.add("Position", vecText(door.position));
    transform.add("Rotation", quatText(door.rotation.x, door.rotation.y, door.rotation.z, door.rotation.w));
    groups.push_back(std::move(transform.group));
    Lines object{.group = WatchGroup{.name = "Object", .lines = {}}};
    object.add("Class", std::string(doorClassName(door.doorClass)));
    object.add("Object type", std::format("{}", door.objectType));
    object.add("Number", std::format("{}", door.number));
    object.add("State", doorStateText(door.state));
    object.add("Angle", std::format("{:.1f} deg (kept {:.1f})", door.angle, door.keptAngle));
    object.add("Hitpoints", std::format("{}/{}", door.hitpoints, door.maxHitpoints));
    object.add("Leaves", std::format("{}{}", door.leaves.size(), door.leavesBroken ? ", broken" : ""));
    object.add("Triangles", std::format("{}, {}", door.triangles[0], door.triangles[1]));
    object.add("Material", std::format("{}", door.material));
    object.add("Pickable", std::format("{} ({} picks abandoned)", yesNo(door.pickable), door.abandonedPicks));
    object.add("Update", std::format("every {} ticks, next in {}", door.interval, door.countdown));
    object.add("Flags", std::format("hittable {}, hidden {}, broken {}, destroying {}, ended {}, removed {}",
                                    yesNo(door.hittable), yesNo(door.hidden), yesNo(door.broken),
                                    yesNo(door.destroying), yesNo(door.ended), yesNo(door.removed)));
    groups.push_back(std::move(object.group));
    return groups;
}

std::vector<WatchGroup> describePane(const world_objects::GlassPane& pane, EntityId id) {
    std::vector<WatchGroup> groups;
    groups.push_back(identity(id, "glass pane", pane.handle, std::format("{}", pane.type)));
    Lines transform{.group = WatchGroup{.name = "Transform", .lines = {}}};
    transform.add("Centre", vecText(pane.centre));
    transform.add("Normal", vecText(pane.normal));
    groups.push_back(std::move(transform.group));
    Lines object{.group = WatchGroup{.name = "Object", .lines = {}}};
    object.add("Size", std::format("{:.2f} x {:.2f} m", pane.width, pane.height));
    object.add("Triangles", std::format("{}, {}", pane.triangles[0], pane.triangles[1]));
    object.add("Broken", yesNo(pane.broken));
    object.add("Hidden", yesNo(pane.hidden));
    object.add("Alarm", yesNo((pane.alarmBits & 3U) == 3U));
    object.add("Window link", pane.link ? std::format("{}", *pane.link) : std::string("-"));
    object.add("Path polygon", pane.polygon ? std::format("{}", *pane.polygon) : std::string("-"));
    groups.push_back(std::move(object.group));
    return groups;
}

std::vector<WatchGroup> describeSpawnRecord(const world_objects::SpawnRecord& record, EntityId id) {
    std::vector<WatchGroup> groups;
    groups.push_back(identity(id, "world object", record.handle, record.typeName));
    Lines transform{.group = WatchGroup{.name = "Transform", .lines = {}}};
    transform.add("Position", vecText(anim::Vec3{record.position[0], record.position[1], record.position[2]}));
    transform.add("Rotation", quatText(record.rotation[0], record.rotation[1], record.rotation[2], record.rotation[3]));
    groups.push_back(std::move(transform.group));
    // Each state bit that is set, by the field's name.
    std::string state;
    for (const auto& [name, on] : std::array<std::pair<std::string_view, bool>, 6>{{{"live", record.live},
                                                                                    {"pinned", record.pinned},
                                                                                    {"removed", record.removed},
                                                                                    {"hidden", record.hidden},
                                                                                    {"owned", record.owned},
                                                                                    {"dying", record.dying}}}) {
        if (on) {
            state += state.empty() ? std::string(name) : " " + std::string(name);
        }
    }
    Lines object{.group = WatchGroup{.name = "Object", .lines = {}}};
    object.add("State", state.empty() ? std::string("-") : state);
    object.add("Flags", std::format("{:#x}", record.flags));
    object.add("Zone", std::format("{}", record.zone));
    object.add("Flag", record.flagName.empty() ? std::string("-") : record.flagName);
    object.add("Tint", std::format("{:#010x}", record.tint));
    object.add("Money", std::format("{}", record.money));
    object.add("Model", record.model ? std::format("{}", *record.model) : std::string("the type's"));
    object.add("Physics body", record.physicsBody ? yesNo(*record.physicsBody) : std::string("not set"));
    object.add("Breaks in", record.breakIn ? std::format("{:.2f} s", *record.breakIn) : std::string("-"));
    groups.push_back(std::move(object.group));
    return groups;
}

} // namespace coney::debug
