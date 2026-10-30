// SPDX-License-Identifier: GPL-3.0-or-later
// The shadow bindings (docs/research/xbox-lighting.md#shadow-light): SetShadowColor, SetShadowLightOffset and
// EnableShadow, empty on the PS2 (0x0017f260, 0x0017f268, 0x0017f270), keep their values as the Xbox port does,
// render-side in the level's lighting.
#include "scripting/shadow_bindings.h"

#include <cstddef>
#include <expected>
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

} // namespace

TEST_CASE("SetShadowColor keeps the level's shadow colour", "[scripting][shadow_bindings]") {
    // Expected: xbox-lighting.md#shadow-light, confirmed (code)
    Harness h;
    CHECK_FALSE(h.lighting.shadow.scripted);
    // global.lua's usual row: (0.77, 0.77, 0.79).
    h.call("SetShadowColor", {Value(0.77), Value(0.77), Value(0.79)});
    CHECK(h.lighting.shadow.scripted);
    CHECK(h.lighting.shadow.colour.r == Approx(0.77F));
    CHECK(h.lighting.shadow.colour.g == Approx(0.77F));
    CHECK(h.lighting.shadow.colour.b == Approx(0.79F));
    CHECK(h.lighting.shadow.colour.a == Approx(1.0F));
    // A missing argument reads 0, as the wrapper's optional numbers (0x0037c608).
    h.call("SetShadowColor", {Value(0.5)});
    CHECK(h.lighting.shadow.colour.r == Approx(0.5F));
    CHECK(h.lighting.shadow.colour.g == Approx(0.0F));
}

TEST_CASE("SetShadowLightOffset keeps the level's shadow light offset in the game's axes",
          "[scripting][shadow_bindings]") {
    // Expected: xbox-lighting.md#shadow-light, confirmed (code)
    Harness h;
    // One of global.lua's rows: (-500, 500, 550).
    h.call("SetShadowLightOffset", {Value(-500.0), Value(500.0), Value(550.0)});
    CHECK(h.lighting.shadow.scripted);
    CHECK(h.lighting.shadow.offset.x == Approx(-500.0F));
    CHECK(h.lighting.shadow.offset.y == Approx(500.0F));
    CHECK(h.lighting.shadow.offset.z == Approx(550.0F));
    // The colour keeps the Xbox default.
    CHECK(h.lighting.shadow.colour.r == Approx(0.7F));
}

TEST_CASE("EnableShadow turns an object's shadow off and on", "[scripting][shadow_bindings]") {
    // Expected: xbox-lighting.md#shadow-map, confirmed (code)
    Harness h;
    CHECK(h.lighting.casters.enabled(12));
    // `EnableShadow(obj, false)`: Lua 4 passes false as nil (0x0037c748 reads it as an optional boolean).
    h.call("EnableShadow", {Value(12.0), Value()});
    CHECK_FALSE(h.lighting.casters.enabled(12));
    h.call("EnableShadow", {Value(12.0), Value(1.0)});
    CHECK(h.lighting.casters.enabled(12));
    // With no second argument the default is false; the handle is truncated.
    h.call("EnableShadow", {Value(13.9)});
    CHECK_FALSE(h.lighting.casters.enabled(13));
    // A number argument is true unless 0.
    h.call("EnableShadow", {Value(13.0), Value(0.0)});
    CHECK_FALSE(h.lighting.casters.enabled(13));
}

TEST_CASE("Without a level's lighting the shadow bindings keep nothing", "[scripting][shadow_bindings]") {
    // Expected: xbox-lighting.md#shadow-light, confirmed (code)
    Harness h;
    h.context.lighting = nullptr;
    h.call("SetShadowColor", {Value(0.1), Value(0.2), Value(0.3)});
    h.call("SetShadowLightOffset", {Value(1.0), Value(2.0), Value(3.0)});
    h.call("EnableShadow", {Value(4.0), Value()});
    CHECK_FALSE(h.lighting.shadow.scripted);
    CHECK(h.lighting.casters.enabled(4));
}
