// SPDX-License-Identifier: GPL-3.0-or-later
// The lighting bindings (docs/references/bindings/effects.md#setlight, docs/research/lighting.md): SetLight's long and
// short forms, its refusals and axes, SetLightFlicker, SetWorldAmbient, SetGammaOffset and the fog.
#include "scripting/lighting_bindings.h"

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "graphics/level_lighting.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

using Catch::Approx;
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

// A script system with Coney's bindings over a level's lighting.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::graphics::LevelLighting lighting;
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
        context.lighting = &lighting;
        scripts.create();
    }

    // The first result of `name(args)`, REQUIRing success; nil when there is none.
    Value call(std::string_view name, const std::vector<Value>& args) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result && !result->empty() ? (*result)[0] : Value();
    }
};

// A Lua table of `values` at keys 1, 2, ...
Value list(std::initializer_list<double> values) {
    auto table = std::make_shared<Table>();
    double key = 1.0;
    for (const double v : values) {
        (void)table->set(Value(key), Value(v));
        key += 1.0;
    }
    return Value(table);
}

// SetLight's 14 long-form arguments for a new point lamp with a corona, `state` and `type`.
std::vector<Value> lampArgs(double light = 0, double state = 1, double type = 0) {
    return {Value(light),
            Value(type),
            list({-286.13, 122.79, 5.49}),
            list({0, 0, -1}),
            list({1, 0.9, 0.8, 1}),
            Value(6.0),
            Value(1.0),
            Value(-0.4),
            Value(0.1),
            Value(1.0),
            Value(3.0),
            Value(2.0),
            Value(4.0),
            Value(state)};
}

} // namespace

TEST_CASE("SetLight's long form makes a light in RenderWare's axes with its corona") {
    Harness h;
    const Value handle = h.call("SetLight", lampArgs());
    REQUIRE(handle.number().value_or(0) > 0);
    const auto* light = h.lighting.lights.light(static_cast<coney::graphics::LightHandle>(*handle.number()));
    REQUIRE(light != nullptr);
    CHECK(light->desc.type == coney::graphics::LightType::Point);
    CHECK(light->desc.position.x == Approx(-286.13F));
    CHECK(light->desc.position.y == Approx(5.49F));
    CHECK(light->desc.position.z == Approx(-122.79F));
    CHECK(light->desc.radius == Approx(6.0F));
    CHECK(light->desc.coronaHeight == Approx(-0.4F));
    CHECK(light->desc.coronaPull == Approx(0.1F));
    CHECK(light->desc.coronaSize == Approx(1.0F));
    CHECK(light->desc.lights == 3);
    CHECK(light->desc.effects == coney::graphics::kFlickerRandom);
    CHECK(light->desc.corona == 3); // the 13th argument 4 is rectangle 3
    CHECK(light->desc.on);
}

TEST_CASE("SetLight refuses bad arguments, changes, switches and removes lights") {
    Harness h;
    CHECK(h.call("SetLight", lampArgs(0, 1, 4)).number().value_or(-1) == 0); // type above 3
    const Value handle = h.call("SetLight", lampArgs());
    const double id = *handle.number();
    // The short form switches it off, then on.
    CHECK(h.call("SetLight", {Value(id), Value(0.0)}).number().value_or(0) == id);
    CHECK_FALSE(h.lighting.lights.light(static_cast<coney::graphics::LightHandle>(id))->desc.on);
    CHECK(h.call("SetLight", {Value(id), Value(1.0)}).number().value_or(0) == id);
    CHECK(h.lighting.lights.light(static_cast<coney::graphics::LightHandle>(id))->desc.on);
    // The long form with the handle changes it.
    std::vector<Value> args = lampArgs(id);
    args[5] = Value(9.0);
    CHECK(h.call("SetLight", args).number().value_or(0) == id);
    CHECK(h.lighting.lights.light(static_cast<coney::graphics::LightHandle>(id))->desc.radius == Approx(9.0F));
    // State 2 removes it.
    CHECK(h.call("SetLight", {Value(id), Value(2.0)}).number().value_or(-1) == 0);
    CHECK(h.lighting.lights.light(static_cast<coney::graphics::LightHandle>(id)) == nullptr);
}

TEST_CASE("SetLightFlicker, SetWorldAmbient, SetGammaOffset and the fog bindings set the level's lighting") {
    Harness h;
    const double id = *h.call("SetLight", lampArgs()).number();
    h.call("SetLightFlicker", {Value(id), Value(100.0), Value(0.0), Value(50.0), Value(0.0), Value(0.0), Value(0.0),
                               Value(0.0), Value(0.0), Value(0.0), Value(30.0)});
    const auto* light = h.lighting.lights.light(static_cast<coney::graphics::LightHandle>(id));
    CHECK((light->desc.effects & coney::graphics::kEffectFlickerMask) == coney::graphics::kFlickerBlink);
    CHECK(light->flicker.dim == 30);

    h.call("SetWorldAmbient", {Value(0.0), Value(0.0), Value(0.0)});
    CHECK(h.lighting.lights.worldAmbientBase().r == Approx(0.07F));
    h.call("SetGammaOffset", {list({0.01, 0.02, 0.03})});
    CHECK(h.lighting.lights.colourOffset().b == Approx(0.03F));

    // level99's fog: (0.05, 0.05, 0.02) → (12, 12, 5).
    h.call("SetFogColor", {Value(0.05), Value(0.05), Value(0.02)});
    CHECK(h.lighting.fog.colour == coney::graphics::Rgba{12, 12, 5, 255});
    h.call("SetFogDistance", {Value(0.25)});
    CHECK(h.lighting.fog.start == Approx(0.25F));
}
