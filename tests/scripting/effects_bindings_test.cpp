// SPDX-License-Identifier: GPL-3.0-or-later
// The effects bindings (docs/references/bindings/effects.md): SpawnParticle's handle, place, rotation and parent, and
// both forms of QueueMotionBlurEffect.
#include "scripting/effects_bindings.h"

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "effects/level_effects.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

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

// A script system with Coney's bindings over a level's effects.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::effects::LevelEffects effects;
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
        context.effects = &effects;
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

TEST_CASE("SpawnParticle makes a system of the type at the place, with a handle of its own", "[effects_bindings]") {
    Harness h;
    const double first = h.first("SpawnParticle", {str("part_fire"), list({1, 2, 3}), list({0, 0, 0, 1}), Value(0.0)})
                             .number()
                             .value_or(0);
    const double second =
        h.first("SpawnParticle", {str("part_train_sound"), list({4, 5, 6}), list({0, 0, 0.5, 0.5}), Value(17.0)})
            .number()
            .value_or(0);
    CHECK(first != 0);
    CHECK(second == first + 1);
    const coney::effects::ParticleSystem* fire = h.effects.particles.find(first);
    REQUIRE(fire != nullptr);
    CHECK(fire->type->name == "part_fire");
    CHECK(fire->position == coney::anim::Vec3{1, 2, 3});
    const coney::effects::ParticleSystem* sound = h.effects.particles.find(second);
    REQUIRE(sound != nullptr);
    CHECK(sound->rotation == coney::anim::Quat{0, 0, 0.5F, 0.5F});
    CHECK(sound->parent == 17);
}

TEST_CASE("QueueMotionBlurEffect blends the strength, or the whole colour from a table", "[effects_bindings]") {
    Harness h;
    h.first("QueueMotionBlurEffect", {Value(150.0), Value(1.0)});
    CHECK(h.effects.motionBlur.target().a == 150);
    h.effects.motionBlur.step(1.0F);
    CHECK(h.effects.motionBlur.current().a == 150);
    h.first("QueueMotionBlurEffect", {list({10, 20, 30, 40}), Value(0.0)});
    CHECK(h.effects.motionBlur.current() == coney::effects::MotionBlur::Colour{10, 20, 30, 40});
}
