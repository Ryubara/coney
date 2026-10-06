// SPDX-License-Identifier: GPL-3.0-or-later
// The effects bindings (docs/references/bindings/effects.md): SpawnParticle's handle, place, rotation and parent, and
// both forms of QueueMotionBlurEffect, and the tag spots' CfgTagSettings and ProcessTag.
#include "scripting/effects_bindings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "effects/ground_fog.h"
#include "effects/level_effects.h"
#include "effects/particles.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/radios.h"
#include "world_objects/tag_spots.h"

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
    coney::world_objects::Radios radios;
    coney::world_objects::TagSpots tagSpots;
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
        context.radios = &radios;
        context.tagSpots = &tagSpots;
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

TEST_CASE("Start3DFog, MaxFogParticles, StartGarbage and EndGarbage reach the level's effects", "[effects_bindings]") {
    Harness h;
    h.first("Start3DFog", {Value(34734080.0), list({10, 20, 30, 40}), Value(0.4), Value(0.5), Value(7.0)});
    const auto& fogSettings = h.effects.fog.settings();
    REQUIRE(fogSettings.has_value());
    if (!fogSettings) {
        return;
    }
    const coney::effects::FogSettings& fog = *fogSettings;
    CHECK(fog.sprite == 34734080U);
    CHECK(fog.colour == std::array<std::uint8_t, 4>{10, 20, 30, 40});
    CHECK(fog.drift == 0.4F);
    CHECK(fog.fadeSpeed == 0.5F);
    CHECK(fog.fadeRate == 7.0F);
    h.first("MaxFogParticles", {Value(15.0)});
    CHECK(h.effects.fog.maxWisps() == 15);
    h.first("StartGarbage", {Value(0.0)});
    CHECK(h.effects.litter.kind() == 0U);
    h.first("EndGarbage");
    CHECK_FALSE(h.effects.litter.kind().has_value());
}

TEST_CASE("CfgSteam configures a steam vent with the colour packed r, g, b, a", "[effects_bindings]") {
    Harness h;
    const double vent = h.first("SpawnParticle", {str("part_steam"), list({1, 2, 3}), list({0, 0, 0, 1}), Value(0.0)})
                            .number()
                            .value_or(0);
    h.first("CfgSteam", {Value(vent), list({128, 64, 32, 200}), Value(10.0), Value(50.0), Value(0.25), Value(0.125),
                         Value(5.5), Value(1.0), Value(0.25), Value(0.0), Value(0.5), Value(1.0)});
    const coney::effects::ParticleSystem* system = h.effects.particles.find(vent);
    REQUIRE(system != nullptr);
    REQUIRE(system->steam.has_value());
    if (!system->steam) {
        return;
    }
    const coney::effects::SteamSettings& steam = *system->steam;
    CHECK(steam.colour == 0x804020C8U);
    CHECK(steam.interval == 10);
    CHECK(steam.puffInterval == 50);
    CHECK(steam.size == 0.25F);
    CHECK(steam.growth == 0.125F);
    CHECK(steam.life == 5.5F);
    CHECK(steam.speed == 1.0F);
    CHECK(steam.rise == 0.25F);
    CHECK(steam.dragH == 0.0F);
    CHECK(steam.dragV == 0.5F);
    CHECK(steam.still);
}

TEST_CASE("SetupRadio makes the object a radio with its callbacks, track and announcement", "[effects_bindings]") {
    Harness h;
    h.first("SetupRadio", {Value(12.0), str("PickUp"), Value(3.0), str("Segment"), Value(2.0)});
    const coney::world_objects::Radio* radio = h.radios.find(12.0);
    REQUIRE(radio != nullptr);
    CHECK(radio->onPickUp == "PickUp");
    CHECK(radio->onSegment == "Segment");
    CHECK(radio->track == 3);
    CHECK(radio->announcementArmed);
    CHECK(radio->next == 2);
}

TEST_CASE("CfgTagSettings and ProcessTag act on a particle system's tag spot only", "[effects_bindings]") {
    Harness h;
    const double tag =
        h.first("SpawnParticle", {str("part_spray_tag"), list({1, 2, 3}), list({0, 0, 0, 1}), Value(0.0)})
            .number()
            .value_or(0);
    h.first("CfgTagSettings", {Value(tag), Value(0x60000), Value(0.25), Value(0.01)});
    const coney::world_objects::TagSpot* spot = h.tagSpots.find(tag);
    REQUIRE(spot != nullptr);
    CHECK(spot->sprite == 0x60000U);
    CHECK(spot->start == 0.25F);
    CHECK(spot->fadeStep == 0.01F);
    CHECK(spot->depth == 1.0F); // the default
    // Not instant: the look now (painted, blank).
    h.first("ProcessTag", {Value(tag), Value(true), Value(false)});
    CHECK(spot->fraction == 1.0F);
    h.first("ProcessTag", {Value(tag), Value(false)});
    CHECK(spot->fraction == 0.0F);
    // Instant: only the next spray's mode.
    h.first("ProcessTag", {Value(tag), Value(false), Value(true)});
    CHECK(spot->sprayMode == coney::world_objects::TagSpot::kWipeOut);
    CHECK(spot->fraction == 0.0F);
    // Not a particle system: nothing.
    h.first("CfgTagSettings", {Value(999.0), Value(1.0)});
    h.first("ProcessTag", {Value(999.0), Value(true)});
    CHECK(h.tagSpots.find(999.0) == nullptr);
}
