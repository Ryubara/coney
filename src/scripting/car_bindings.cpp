// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/car_bindings.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include "scripting/binding_args.h"
#include "world_objects/cars.h"

namespace coney::script {

namespace {

// The value of the global `NilHandle`: what `CarSpawn` returns when the pool is full.
constexpr double kNilHandle = 0.0;

// Argument `i` as tolua reads an unsigned integer: truncated, then kept to 32 bits; as a handle.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(
        static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, i)))));
}

// The table argument `i`'s first `N` numbers (t[1]..t[N]); 0 for a missing one, as tolua reads it.
template <std::size_t N> std::array<float, N> tableArg(std::span<const Value> args, std::size_t i) {
    std::array<float, N> values{};
    if (i >= args.size() || args[i].table() == nullptr) {
        return values;
    }
    for (std::size_t k = 0; k < N; ++k) {
        values.at(k) =
            static_cast<float>(args[i].table()->get(Value(static_cast<double>(k + 1))).number().value_or(0.0));
    }
    return values;
}

// `CarSpawn(typeName, pos, rot, unused, unused2)`: a car and its handle; the last two are not read.
// @orig 0x00378320 CarSpawn (unknown)
NativeFunction makeCarSpawn(const BindingContext& context, std::function<double()> nextHandle) {
    // The context's cars are read at each call: gameplay sets them when a level is entered, after the bindings.
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        world_objects::Cars* cars = context->cars;
        const double handle = nextHandle();
        if (cars == nullptr) {
            return binding::number(handle);
        }
        const std::array<float, 3> p = tableArg<3>(args, 1);
        const std::array<float, 4> q = tableArg<4>(args, 2);
        const world_objects::Car* car = cars->spawn(binding::string(args, 0), anim::Vec3{p[0], p[1], p[2]},
                                                    anim::Quat{q[0], q[1], q[2], q[3]}, handle);
        return binding::number(car != nullptr ? handle : kNilHandle);
    };
}

// `CarSetColor(car, {c1, c2, c3, c4})`.
// @orig 0x003785c8 CarSetColor (unknown)
NativeFunction makeCarSetColor(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (world_objects::Cars* cars = context->cars; cars != nullptr) {
            cars->setColour(handleArg(args, 0), tableArg<4>(args, 1));
        }
        return binding::none();
    };
}

// `CarMakeGoodAsNew(car)`.
// @orig 0x00378808 CarMakeGoodAsNew (unknown)
NativeFunction makeCarMakeGoodAsNew(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (world_objects::Cars* cars = context->cars; cars != nullptr) {
            cars->repair(handleArg(args, 0));
        }
        return binding::none();
    };
}

// `CarSpawnRadio(car)`.
// @orig 0x0036de50 CarSpawnRadio (unknown)
NativeFunction makeCarSpawnRadio(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (world_objects::Cars* cars = context->cars; cars != nullptr) {
            cars->spawnRadio(handleArg(args, 0));
        }
        return binding::none();
    };
}

} // namespace

void addCarBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle) {
    vm.registerFunction("CarMakeGoodAsNew", makeCarMakeGoodAsNew(context));
    vm.registerFunction("CarSetColor", makeCarSetColor(context));
    vm.registerFunction("CarSpawn", makeCarSpawn(context, std::move(nextHandle)));
    vm.registerFunction("CarSpawnRadio", makeCarSpawnRadio(context));
}

} // namespace coney::script
