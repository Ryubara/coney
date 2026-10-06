// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/object_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "scripting/binding_args.h"
#include "world_objects/level_objects.h"
#include "world_objects/nav_links.h"

namespace coney::script {

namespace {

using world_objects::LevelObjects;

// The value of `NilHandle`, what a binding returns for no object (script_bindings.cpp sets the global).
constexpr double kNilHandle = 0.0;
// `CfgObj`'s arguments the doors read (0-based): class, hitpoints, box, material, type.
constexpr std::size_t kCfgObjClass = 1;
constexpr std::size_t kCfgObjHitpoints = 2;
constexpr std::size_t kCfgObjSize = 7;
constexpr std::size_t kCfgObjMaterial = 12;
constexpr std::size_t kCfgObjType = 18;
// `CfgWarriorClass`'s argument (0-based) that sets the record's byte `+0x0a`, the lock pick's difficulty plus 1.
constexpr std::size_t kCfgWarriorClassLockPick = 10;
// Its argument that sets byte `+0x09`, the tagging difficulty (1-3).
constexpr std::size_t kCfgWarriorClassTag = 9;

// Argument `i` truncated to a whole number, as tolua reads an integer.
int intArg(std::span<const Value> args, std::size_t i) {
    return static_cast<int>(std::trunc(binding::number(args, i)));
}

// Argument `i` as a boolean as tolua reads one: anything but nil (and an absent argument: `fallback`).
bool boolArg(std::span<const Value> args, std::size_t i, bool fallback) {
    return i < args.size() ? !args[i].isNil() : fallback;
}

// Argument `i` as `N` numbers t[1]..t[N] of a table; zeros for what is missing, as tolua reads it.
template <std::size_t N> std::array<float, N> numbersArg(std::span<const Value> args, std::size_t i) {
    std::array<float, N> out{};
    if (i >= args.size() || args[i].table() == nullptr) {
        return out;
    }
    for (std::size_t k = 0; k < N; ++k) {
        out.at(k) = static_cast<float>(args[i].table()->get(Value(static_cast<double>(k + 1))).number().value_or(0.0));
    }
    return out;
}

// Argument `i` as a point.
anim::Vec3 pointArg(std::span<const Value> args, std::size_t i) {
    const std::array<float, 3> p = numbersArg<3>(args, i);
    return anim::Vec3{p[0], p[1], p[2]};
}

// Where the object `handle` is: a pane, door or leaf, a human the scripts made, or a flag; nothing otherwise.
std::optional<anim::Vec3> positionOf(const BindingContext& context, double handle) {
    if (context.objects != nullptr) {
        if (const std::optional<anim::Vec3> at = context.objects->positionOf(handle)) {
            return at;
        }
    }
    if (context.humans != nullptr) {
        if (const std::optional<world_objects::Placement> human = context.humans->placement(handle)) {
            return anim::Vec3{human->position[0], human->position[1], human->position[2]};
        }
    }
    if (context.flags != nullptr) {
        if (const world_objects::WorldFlag* flag = context.flags->find(handle)) {
            return anim::Vec3{flag->position[0], flag->position[1], flag->position[2]};
        }
    }
    return std::nullopt;
}

// `SpawnBreakableGlass(type, corner, cornerU, cornerV, uv0, uv1, flag, triangle1, triangle2)`: a pane; its handle.
// @orig 0x0039c0e0 Glass_Spawn (unknown)
NativeFunction makeSpawnBreakableGlass(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        const double handle = nextHandle();
        if (objects != nullptr) {
            const world_objects::GlassSpawn spawn{.type = intArg(args, 0),
                                                  .corner = pointArg(args, 1),
                                                  .cornerU = pointArg(args, 2),
                                                  .cornerV = pointArg(args, 3),
                                                  .uv0 = numbersArg<2>(args, 4),
                                                  .uv1 = numbersArg<2>(args, 5),
                                                  .flag = intArg(args, 6),
                                                  .triangles = {static_cast<std::uint32_t>(intArg(args, 7)),
                                                                static_cast<std::uint32_t>(intArg(args, 8))}};
            objects->glass.spawn(handle, spawn, objects->world);
        }
        return binding::number(handle);
    };
}

// `SpawnDoor(type, pos, rot, {triangle1, triangle2}, number)`: a door or barrier, by its type's CfgObj; its handle.
// @orig 0x00397230 Door_Spawn (unknown)
NativeFunction makeSpawnDoor(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, recorded = context.recorded,
            nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        const double handle = nextHandle();
        if (objects != nullptr) {
            const std::array<float, 4> q = numbersArg<4>(args, 2);
            const std::array<float, 2> triangles = numbersArg<2>(args, 3);
            const world_objects::DoorSpawn spawn{
                .type = binding::string(args, 0),
                .position = pointArg(args, 1),
                .rotation = anim::Quat{q[0], q[1], q[2], q[3]},
                .triangles = {static_cast<std::uint32_t>(triangles[0]), static_cast<std::uint32_t>(triangles[1])},
                .number = static_cast<std::uint16_t>(intArg(args, 4))};
            const std::optional<world_objects::ObjectTypeInfo> info = objectTypeFromCfgObj(recorded, spawn.type);
            objects->doors.spawn(handle, spawn, info ? &*info : nullptr, nextHandle, objects->world);
        }
        return binding::number(handle);
    };
}

// Sets the glass type `CfgSetGlassProperties(type, windowLink, alarm, sprite, brokenSprite)` names in `glass`.
void setGlassType(world_objects::GlassPanes& glass, std::span<const Value> args) {
    glass.setType(intArg(args, 0),
                  world_objects::GlassType{.windowLink = boolArg(args, 1, false),
                                           .alarm = boolArg(args, 2, false),
                                           .sprite = static_cast<std::uint32_t>(intArg(args, 3)),
                                           .brokenSprite = static_cast<std::uint32_t>(intArg(args, 4))});
}

// `CfgSetGlassProperties(type, windowLink, alarm, sprite, brokenSprite)`: one glass type; also recorded.
// @orig 0x0038fab8 GlassTypes_Set (unknown)
NativeFunction makeCfgSetGlassProperties(const BindingContext& context) {
    return [context = &context, recorded = context.recorded](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        if (recorded != nullptr) {
            recorded->add("CfgSetGlassProperties", args);
        }
        if (objects != nullptr) {
            setGlassType(objects->glass, args);
        }
        return binding::none();
    };
}

// A binding that sends the door its first argument names the state command `command` (`OpenDoor`, `CloseDoor`,
// `DisableDoorCollision`).
NativeFunction makeDoorCommand(const BindingContext& context, int command) {
    return [context = &context, command](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        if (objects != nullptr) {
            objects->doors.command(binding::number(args, 0), command, objects->world);
        }
        return binding::none();
    };
}

// `ObjectChangeState(object, state)`: message 0x22 with the state.
NativeFunction makeObjectChangeState(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        if (objects != nullptr) {
            objects->doors.command(binding::number(args, 0), intArg(args, 1), objects->world);
        }
        return binding::none();
    };
}

// `SetDoorPickable(door, pickable)`: pickable on (the default) or off.
// @orig 0x00397078 Door_SetPickable (unknown)
NativeFunction makeSetDoorPickable(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        if (objects != nullptr) {
            objects->doors.setPickable(binding::number(args, 0), boolArg(args, 1, true), objects->world);
        }
        return binding::none();
    };
}

// `DoorOpen(door, human)` (and **Coney's stand-in** for `OpenDoorAnimated(door, human)`, whose human state 26 is not
// traced): swings the door open away from the human; from the door itself when the human is unknown.
// @orig 0x003f9910 DoorSwing_OpenBy (unknown)
NativeFunction makeDoorOpen(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (LevelObjects* objects = context->objects) {
            const double door = binding::number(args, 0);
            const std::optional<anim::Vec3> doorAt = objects->positionOf(door);
            const std::optional<anim::Vec3> humanAt = positionOf(*context, binding::number(args, 1));
            objects->doors.openBy(door, humanAt.value_or(doorAt.value_or(anim::Vec3{})), objects->world);
        }
        return binding::none();
    };
}

// `DoorOpenDegree(door, degrees)`: message 0x42.
NativeFunction makeDoorOpenDegree(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        if (objects != nullptr) {
            objects->doors.openToDegree(binding::number(args, 0), static_cast<float>(intArg(args, 1)), objects->world);
        }
        return binding::none();
    };
}

// `IsDoorOpen(door)`: message 0x0c, true only in state 5.
NativeFunction makeIsDoorOpen(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        return binding::boolean(objects != nullptr && objects->doors.isOpen(binding::number(args, 0)));
    };
}

// `GetHitpoints(object)`: a door's or barrier's hitpoints; 0 for anything else.
NativeFunction makeGetHitpoints(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        return binding::number(objects != nullptr ? objects->doors.hitpoints(binding::number(args, 0)) : 0);
    };
}

// `GetLeftDoorHandle(door)` / `GetRightDoorHandle(door)` (message 0x10): a leaf's handle, NilHandle without one.
NativeFunction makeLeafHandle(const BindingContext& context, std::size_t leaf) {
    return [context = &context, leaf](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        const world_objects::Door* door = objects != nullptr ? objects->doors.find(binding::number(args, 0)) : nullptr;
        if (door == nullptr || leaf >= door->leaves.size()) {
            return binding::number(kNilHandle);
        }
        return binding::number(door->leaves[leaf].handle);
    };
}

// `BreakGlassInRadius(centre, radius)`: every pane within the radius of the object `centre`, and the glass objects.
// @orig 0x003966c8 BreakGlassInRadius (unknown)
NativeFunction makeBreakGlassInRadius(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (LevelObjects* objects = context->objects) {
            if (const std::optional<anim::Vec3> centre = positionOf(*context, binding::number(args, 0))) {
                objects->glass.breakInRadius(*centre, static_cast<float>(binding::number(args, 1)), objects->world);
            }
        }
        return binding::none();
    };
}

// `BreakObjectsInRadius(centre, radius)`: message 0x15 to every door within the radius of the object `centre`.
// @orig 0x00396390 BreakObjectsInRadius (unknown)
NativeFunction makeBreakObjectsInRadius(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (LevelObjects* objects = context->objects) {
            if (const std::optional<anim::Vec3> centre = positionOf(*context, binding::number(args, 0))) {
                objects->doors.destroyInRadius(*centre, static_cast<float>(binding::number(args, 1)));
            }
        }
        return binding::none();
    };
}

// `DisableDoorLink(pos)`, `EnableDoorLink(pos)`, `ConvertJumpToDoor(pos)`: the nearest link within 5 m and its reverse.
NativeFunction makeLinkByPosition(const BindingContext& context, void (world_objects::NavLinks::*change)(anim::Vec3)) {
    return [context = &context, change](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        if (objects != nullptr) {
            world_objects::NavLinks links(objects->world.paths);
            (links.*change)(pointArg(args, 0));
        }
        return binding::none();
    };
}

// `CfgSetLockPickHandler(start, stop, success)`: the lock pick's three callbacks.
NativeFunction makeCfgSetLockPickHandler(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        if (objects != nullptr) {
            objects->lockPick.start = binding::string(args, 0);
            objects->lockPick.stop = binding::string(args, 1);
            objects->lockPick.success = binding::string(args, 2);
        }
        return binding::none();
    };
}

// `CfgSetLockPickStageFailHandler(fn)`: the callback of a missed press.
NativeFunction makeCfgSetLockPickStageFailHandler(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        world_objects::LevelObjects* const objects = context->objects;
        if (objects != nullptr) {
            objects->lockPick.stageFail = binding::string(args, 0);
        }
        return binding::none();
    };
}

} // namespace

std::optional<world_objects::ObjectTypeInfo> objectTypeFromCfgObj(const RecordedCalls* recorded,
                                                                  std::string_view name) {
    if (recorded == nullptr) {
        return std::nullopt;
    }
    for (const std::vector<Value>& call : recorded->calls("CfgObj")) {
        const std::span<const Value> args(call);
        if (binding::string(args, 0) != name) {
            continue;
        }
        world_objects::ObjectTypeInfo info;
        info.className = binding::string(args, kCfgObjClass);
        info.hitpoints = intArg(args, kCfgObjHitpoints);
        const std::array<float, 3> size = numbersArg<3>(args, kCfgObjSize);
        info.size = anim::Vec3{size[0], size[1], size[2]};
        info.material = static_cast<std::uint8_t>(intArg(args, kCfgObjMaterial));
        info.objectType = intArg(args, kCfgObjType);
        return info;
    }
    return std::nullopt;
}

void applyRecordedGlassTypes(const RecordedCalls& recorded, world_objects::GlassPanes& glass) {
    for (const std::vector<Value>& call : recorded.calls("CfgSetGlassProperties")) {
        setGlassType(glass, std::span<const Value>(call));
    }
}

int lockPickDifficulty(const RecordedCalls* recorded, int warriorClass) {
    if (recorded == nullptr) {
        return 0;
    }
    // The record keeps the last write, so the last call for the class wins.
    int difficulty = 0;
    for (const std::vector<Value>& call : recorded->calls("CfgWarriorClass")) {
        const std::span<const Value> args(call);
        if (intArg(args, 0) == warriorClass) {
            difficulty = std::clamp(intArg(args, kCfgWarriorClassLockPick) - 1, 0, 2);
        }
    }
    return difficulty;
}

int tagDifficulty(const RecordedCalls* recorded, int warriorClass) {
    if (recorded == nullptr) {
        return 0;
    }
    // The record keeps the last write, so the last call for the class wins.
    int difficulty = 0;
    for (const std::vector<Value>& call : recorded->calls("CfgWarriorClass")) {
        const std::span<const Value> args(call);
        if (intArg(args, 0) == warriorClass) {
            difficulty = intArg(args, kCfgWarriorClassTag);
        }
    }
    return difficulty;
}

void addObjectBindings(LuaVm& vm, const BindingContext& context, const std::function<double()>& nextHandle) {
    vm.registerFunction("SpawnBreakableGlass", makeSpawnBreakableGlass(context, nextHandle));
    vm.registerFunction("SpawnDoor", makeSpawnDoor(context, nextHandle));
    vm.registerFunction("CfgSetGlassProperties", makeCfgSetGlassProperties(context));
    vm.registerFunction("OpenDoor", makeDoorCommand(context, world_objects::door_command::kOpen));
    vm.registerFunction("CloseDoor", makeDoorCommand(context, world_objects::door_command::kClose));
    vm.registerFunction("DisableDoorCollision",
                        makeDoorCommand(context, world_objects::door_command::kDisableCollision));
    vm.registerFunction("ObjectChangeState", makeObjectChangeState(context));
    vm.registerFunction("SetDoorPickable", makeSetDoorPickable(context));
    vm.registerFunction("DoorOpen", makeDoorOpen(context));
    vm.registerFunction("OpenDoorAnimated", makeDoorOpen(context));
    vm.registerFunction("DoorOpenDegree", makeDoorOpenDegree(context));
    vm.registerFunction("IsDoorOpen", makeIsDoorOpen(context));
    vm.registerFunction("GetHitpoints", makeGetHitpoints(context));
    vm.registerFunction("GetLeftDoorHandle", makeLeafHandle(context, 0));
    vm.registerFunction("GetRightDoorHandle", makeLeafHandle(context, 1));
    vm.registerFunction("BreakGlassInRadius", makeBreakGlassInRadius(context));
    vm.registerFunction("BreakObjectsInRadius", makeBreakObjectsInRadius(context));
    vm.registerFunction("DisableDoorLink", makeLinkByPosition(context, &world_objects::NavLinks::disableNear));
    vm.registerFunction("EnableDoorLink", makeLinkByPosition(context, &world_objects::NavLinks::enableNear));
    vm.registerFunction("ConvertJumpToDoor", makeLinkByPosition(context, &world_objects::NavLinks::convertJumpToDoor));
    vm.registerFunction("CfgSetLockPickHandler", makeCfgSetLockPickHandler(context));
    vm.registerFunction("CfgSetLockPickStageFailHandler", makeCfgSetLockPickStageFailHandler(context));
}

} // namespace coney::script
