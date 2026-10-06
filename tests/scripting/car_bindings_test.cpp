// SPDX-License-Identifier: GPL-3.0-or-later
// The car bindings (docs/references/bindings/world.md): CarSpawn's handle and place, CarSetColor, CarMakeGoodAsNew and
// CarSpawnRadio by handle.
#include "scripting/car_bindings.h"

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/cars.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Table;
using coney::script::Value;

namespace {

// A host that ignores every request: these bindings ask nothing of it.
class QuietHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// A script system with Coney's bindings over a level's parked cars.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::world_objects::Cars cars;
    coney::script::BindingContext context;
    ScriptSystem scripts;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.cars = &cars;
        scripts.create();
    }

    // The first result of `name(args)`; REQUIREs success, nil when there is none.
    Value first(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result && !result->empty() ? (*result)[0] : Value();
    }
};

// A Lua string value.
Value str(const char* text) { return Value(std::string(text)); }

// A table of numbers at 1, 2, ...
Value list(std::initializer_list<double> numbers) {
    auto table = std::make_shared<Table>();
    double key = 1;
    for (const double n : numbers) {
        REQUIRE(table->set(Value(key), Value(n)).has_value());
        key += 1;
    }
    return Value(table);
}

} // namespace

TEST_CASE("CarSpawn makes a car of the type at the place, with a handle of its own", "[car_bindings]") {
    Harness h;
    const double first =
        h.first("CarSpawn", {str("car_coupe"), list({1, 2, 3}), list({0, 0, 0, 1}), Value(-1.0), Value(0.0)})
            .number()
            .value_or(0);
    const double second =
        h.first("CarSpawn", {str("car_osedan"), list({4, 5, 6}), list({0, 0, 1, 0}), Value(-1.0), Value(0.0)})
            .number()
            .value_or(0);
    CHECK(first != 0);
    CHECK(second == first + 1);
    const coney::world_objects::Car* coupe = h.cars.find(first);
    REQUIRE(coupe != nullptr);
    CHECK(coupe->type == 1);
    CHECK(coupe->position == coney::anim::Vec3{1, 2, 3});
    REQUIRE(h.cars.find(second) != nullptr);
    CHECK(h.cars.find(second)->rotation == coney::anim::Quat{0, 0, 1, 0});
}

TEST_CASE("the car bindings paint, repair and put a stereo in the car they name", "[car_bindings]") {
    Harness h;
    const double car =
        h.first("CarSpawn", {str("car_wagon"), list({0, 0, 0}), list({0, 0, 0, 1})}).number().value_or(0);
    h.first("CarSetColor", {Value(car), list({1, 0, 0, 1})});
    REQUIRE(h.cars.find(car) != nullptr);
    CHECK(coney::world_objects::paintOf(h.cars.find(car)->paint[0]) == coney::world_objects::CarPaint{255, 0, 0, 255});
    h.cars.removePart(car, 5, true);
    h.first("CarMakeGoodAsNew", {Value(car)});
    CHECK(h.cars.find(car)->removedParts == 0);
    h.first("CarSpawnRadio", {Value(car)});
    CHECK(h.cars.find(car)->stereo == coney::world_objects::StereoState::InCar);
    h.first("CarSpawnRadio", {Value(car + 50)}); // not a car: ignored
}
