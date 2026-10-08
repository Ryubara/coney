// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/spawn_bindings.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include "scripting/binding_args.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// The value of the global `NilHandle`: what `ObjSpawn` returns for a suppressed object.
constexpr double kNilHandle = 0.0;
// `ObjSpawn`'s default tint: white, no tint.
constexpr double kNoTint = 4294967295.0;

// Argument `i` truncated to an unsigned 32-bit number, as tolua reads one (a negative number wraps).
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, i))));
}

// Argument `i` as a quaternion: a table of four numbers at t[1]..t[4]; missing entries are 0, as tolua reads them,
// and anything that is not a table is the identity.
std::array<float, 4> quaternionArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].table() == nullptr) {
        return {0, 0, 0, 1};
    }
    const Table& table = *args[i].table();
    std::array<float, 4> rotation{};
    for (std::size_t c = 0; c < rotation.size(); ++c) {
        rotation.at(c) = static_cast<float>(table.get(Value(static_cast<double>(c + 1))).number().value_or(0.0));
    }
    return rotation;
}

// `CfgObj(name, className, hitpoints, ...)`: one type of the object database. The arguments are recorded too, for
// what reads them before Coney models the rest of the record.
// @orig 0x0036a1a0 CfgObj (unknown)
NativeFunction makeCfgObj(const BindingContext& context) {
    return [types = context.objectTypes, recorded = context.recorded](std::span<const Value> args) {
        if (recorded != nullptr) {
            recorded->add("CfgObj", args);
        }
        if (types != nullptr) {
            // The arguments by their place (0-based): 3 and 4 the bytes `+0x5a` and `+0x5b`, 12 the material `+0x64`,
            // 13 the pick-up animation `+0x65`, 18 the kind `+0x86` and 19 the anim set `+0x87`, each kept as a byte;
            // 11 the body word `+0x5e` (16 bits); 16 the float `+0x70`, a held object's grip; 6 and 7 the body's centre
            // and size (tables of three numbers) and 8 its shape `+0x84`.
            constexpr std::size_t kBodyCentreArg = 6;
            constexpr std::size_t kBodySizeArg = 7;
            constexpr std::size_t kBodyShapeArg = 8;
            constexpr std::size_t kValueArg = 3;
            constexpr std::size_t kSecondHitsArg = 4;
            constexpr std::size_t kBodyWordArg = 11;
            constexpr std::size_t kMaterialArg = 12;
            constexpr std::size_t kPickupAnimArg = 13;
            constexpr std::size_t kObjectKindArg = 18;
            constexpr std::size_t kAnimSetArg = 19;
            constexpr std::size_t kGripArg = 16;
            const auto byte = [&args](std::size_t i) {
                return static_cast<int>(static_cast<std::uint32_t>(std::trunc(binding::number(args, i))) & 0xffU);
            };
            world_objects::ObjectType type;
            type.name = binding::string(args, 0);
            type.className = binding::string(args, 1);
            type.hitpoints = static_cast<int>(std::trunc(binding::number(args, 2)));
            type.value = byte(kValueArg);
            type.pickupAnim = byte(kPickupAnimArg);
            type.objectKind = byte(kObjectKindArg);
            type.animSet = byte(kAnimSetArg);
            type.grip = static_cast<float>(binding::number(args, kGripArg));
            // 20 and 21: the pose held or worn, a missing coordinate 0 as tolua reads it.
            constexpr std::size_t kHoldPositionArg = 20;
            constexpr std::size_t kHoldRotationArg = 21;
            const std::array<float, 3> hold =
                binding::position(args, kHoldPositionArg).value_or(std::array<float, 3>{});
            type.holdPosition = anim::Vec3{hold[0], hold[1], hold[2]};
            const std::array<float, 4> turn = quaternionArg(args, kHoldRotationArg);
            type.holdRotation = anim::Quat{turn[0], turn[1], turn[2], turn[3]};
            type.secondHits = byte(kSecondHitsArg);
            type.bodyWord = static_cast<std::uint16_t>(unsignedArg(args, kBodyWordArg) & 0xffffU);
            type.material = static_cast<std::uint8_t>(byte(kMaterialArg));
            type.bodyCentre = binding::position(args, kBodyCentreArg).value_or(std::array<float, 3>{});
            type.bodySize = binding::position(args, kBodySizeArg).value_or(std::array<float, 3>{});
            type.bodyShape = byte(kBodyShapeArg);
            types->add(std::move(type));
        }
        return binding::none();
    };
}

// `ObjSpawn(typeName, pos, rot, unused, zone, flags, tint, flagName)`: a spawn record and its handle, or NilHandle
// when the object is suppressed (spawnAllowed()) or the pool is full. The object is made when the handle is resolved.
// @orig 0x00377cc8 ObjSpawn (unknown)
NativeFunction makeObjSpawn(const BindingContext& context, std::function<double()> nextHandle) {
    return [records = context.spawnRecords, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        world_objects::SpawnRecord record;
        record.typeName = binding::string(args, 0);
        if (!world_objects::spawnAllowed(record.typeName)) {
            return binding::number(kNilHandle);
        }
        record.handle = nextHandle();
        if (records == nullptr) {
            return binding::number(record.handle);
        }
        // tolua reads a missing coordinate as 0.
        record.position = binding::position(args, 1).value_or(std::array<float, 3>{});
        record.rotation = quaternionArg(args, 2);
        record.zone = unsignedArg(args, 4);
        record.flags = unsignedArg(args, 5);
        record.tint = args.size() > 6 && !args[6].isNil() ? unsignedArg(args, 6) : static_cast<std::uint32_t>(kNoTint);
        record.flagName = binding::string(args, 7);
        const double handle = record.handle;
        return binding::number(records->add(std::move(record)) != nullptr ? handle : kNilHandle);
    };
}

} // namespace

void addSpawnBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle) {
    vm.registerFunction("CfgObj", makeCfgObj(context));
    vm.registerFunction("ObjSpawn", makeObjSpawn(context, std::move(nextHandle)));
}

} // namespace coney::script
