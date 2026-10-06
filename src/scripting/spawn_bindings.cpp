// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/spawn_bindings.h"

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
            // The kind (argument 20) is kept as a byte (`+0x86`).
            constexpr std::size_t kObjectKindArg = 19;
            types->add(binding::string(args, 0), binding::string(args, 1),
                       static_cast<int>(std::trunc(binding::number(args, 2))),
                       static_cast<int>(static_cast<std::uint32_t>(std::trunc(binding::number(args, kObjectKindArg))) &
                                        0xffU));
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
