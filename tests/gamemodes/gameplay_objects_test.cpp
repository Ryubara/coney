// SPDX-License-Identifier: GPL-3.0-or-later
// Gameplay's glass panes and doors (docs/research/objects.md#coneys-implementation): the boot scripts' glass types
// applied to the level's objects, the level script's spawns kept by gameplay and handed to the level, the lock pick's
// callbacks reaching the scripts, the CrimeScene flag a break-in moves, and the objects cleared when the level ends.
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "effects/particles.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_object_services.h"
#include "gamemodes/level_start.h"
#include "support/lua_fixtures.h"
#include "support/recording_device.h"
#include "world_objects/level_objects.h"

namespace {

// The scripts: config_preload2.lua sets glass type 3 alarmed (as the boot's preload does, before any level);
// config_preload3.lua lists level1; level1.lua configures and spawns a door and spawns a pane of type 3, and
// global.lua defines OnPick, which sets the global `picked`.
std::map<std::string, std::vector<std::byte>, std::less<>> objectScripts() {
    using coney::test::LuaAsm;
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    for (const char* empty : {"enum_preload.lua", "config_preload.lua"}) {
        files[empty] = coney::test::luaChunk(LuaAsm().end());
    }
    LuaAsm glass;
    glass.getGlobal("CfgSetGlassProperties").pushInt(3).pushNil().pushInt(1).pushInt(5).pushInt(6).call(5);
    files["config_preload2.lua"] = coney::test::luaChunk(glass.end());
    LuaAsm levels;
    levels.getGlobal("CfgLevelName").pushInt(1).pushString("level1").pushString("").pushString("level1");
    levels.pushString("").pushInt(1).call(6);
    files["config_preload3.lua"] = coney::test::luaChunk(levels.end());

    // global.lua: OnPick = function() picked = 1 end
    LuaAsm onPick;
    onPick.pushInt(1).setGlobal("picked");
    LuaAsm global;
    global.spec.protos = {onPick.end()};
    global.closure(0).setGlobal("OnPick");
    files["global.lua"] = coney::test::luaChunk(global.end());

    LuaAsm level;
    level.getGlobal("CfgObj").pushString("dyn_door_wood").pushString("dyn_door_swinging").pushInt(100).call(3);
    level.getGlobal("SpawnDoor").pushString("dyn_door_wood").call(1);
    level.getGlobal("SpawnBreakableGlass").pushInt(3).call(1);
    files["level1.lua"] = coney::test::luaChunk(level.end());
    return files;
}

// A loaded level that does nothing.
class EmptyLevel final : public coney::GameMode {
  public:
    [[nodiscard]] std::uint32_t id() const override { return 0x1ff; }
    coney::ModeResult update(coney::GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        return coney::ModeResult::Stay;
    }
};

} // namespace

TEST_CASE("gameplay keeps the level script's panes and doors and hands them to the level", "[gameplay][objects]") {
    const auto files = objectScripts();
    const coney::script::ScriptSource source =
        [&files](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
        const auto found = files.find(name);
        if (found == files.end()) {
            return coney::fail(coney::ErrorCode::NotFound, "no such script");
        }
        return found->second;
    };
    coney::LevelScripts scripts(source, "level1", 1, [](std::string_view) {});
    // The boot's glass type was recorded with no level's objects to set it in.
    CHECK(scripts.context().objects == nullptr);
    CHECK(scripts.recorded().count("CfgSetGlassProperties") == 1);

    coney::test::RecordingDevice device;
    coney::world_objects::LevelObjects* handed = nullptr;
    std::size_t doorsAtLoad = 0;
    std::size_t panesAtLoad = 0;
    coney::GameplayMode gameplay(
        device, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(), scripts.flags(),
        scripts.recorded(),
        [&](const coney::LevelStart& /*start*/,
            const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
            handed = cast.objects;
            doorsAtLoad = cast.objects != nullptr ? cast.objects->doors.doors().size() : 0;
            panesAtLoad = cast.objects != nullptr ? cast.objects->glass.panes().size() : 0;
            return std::make_unique<EmptyLevel>();
        },
        [](std::string_view) {});
    gameplay.setLevel("level1");
    gameplay.enter();

    // The level got gameplay's objects with what the script spawned, typed by the boot's glass type.
    CHECK(handed == &gameplay.objects());
    CHECK(scripts.context().objects == &gameplay.objects());
    CHECK(doorsAtLoad == 1);
    CHECK(panesAtLoad == 1);
    const coney::world_objects::GlassType* type = gameplay.objects().glass.type(3);
    REQUIRE(type != nullptr);
    CHECK(type->alarm);
    REQUIRE(gameplay.objects().doors.doors().size() == 1);
    CHECK(gameplay.objects().doors.doors()[0].hitpoints == 100);
    // The objects' services reach the scripts and the flags.
    REQUIRE(gameplay.objects().world.services != nullptr);
    gameplay.objects().world.services->callScript("OnPick", 1.0, 2.0);
    CHECK(scripts.scripts().vm().global("picked").number() == 1.0);
    gameplay.objects().world.services->moveCrimeSceneFlag({7.0F, 8.0F, 9.0F});
    const std::optional<double> crimeScene = scripts.flags().findByName(coney::kCrimeSceneFlag);
    REQUIRE(crimeScene.has_value());
    const coney::world_objects::WorldFlag* flag = scripts.flags().find(crimeScene.value_or(0.0));
    REQUIRE(flag != nullptr);
    CHECK(flag->position == std::array<float, 3>{7.0F, 8.0F, 9.0F});

    // Leaving the level forgets its objects, but keeps the glass types.
    gameplay.exit();
    CHECK(gameplay.objects().doors.doors().empty());
    CHECK(gameplay.objects().glass.panes().empty());
    CHECK(gameplay.objects().world.collision == nullptr);
    CHECK(gameplay.objects().glass.type(3)->alarm);
}

TEST_CASE("the level's objects' services pass the sounds on", "[gameplay][objects]") {
    struct Counting final : coney::world_objects::ObjectServices {
        int sounds = 0;
        int pairs = 0;
        int clicks = 0;
        void playSound(std::uint32_t /*nameHash*/, coney::anim::Vec3 /*at*/) override { ++sounds; }
        void playMaterialPair(std::uint8_t /*a*/, std::uint8_t /*b*/, coney::anim::Vec3 /*at*/,
                              float /*volume*/ = 1.0F) override {
            ++pairs;
        }
        void lockPickClick(double /*human*/) override { ++clicks; }
    };
    const auto files = objectScripts();
    const coney::script::ScriptSource source =
        [&files](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
        const auto found = files.find(name);
        if (found == files.end()) {
            return coney::fail(coney::ErrorCode::NotFound, "no such script");
        }
        return found->second;
    };
    coney::LevelScripts scripts(source, "level1", 1, [](std::string_view) {});
    Counting counting;
    coney::LevelObjectServices services(scripts.scripts(), scripts.flags(), nullptr);
    services.playSound(1, {});
    services.callScript("", 0.0, 0.0); // no name: nothing
    services.setSounds(&counting);
    services.playSound(1, {});
    services.playMaterialPair(2, 5, {});
    services.lockPickClick(1.0);
    CHECK(counting.sounds == 1);
    CHECK(counting.pairs == 1);
    CHECK(counting.clicks == 1);
}

TEST_CASE("the level's objects' services pass a human's damage to gameplay's receiver", "[gameplay][objects]") {
    const auto files = objectScripts();
    const coney::script::ScriptSource source =
        [&files](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
        const auto found = files.find(name);
        if (found == files.end()) {
            return coney::fail(coney::ErrorCode::NotFound, "no such script");
        }
        return found->second;
    };
    coney::LevelScripts scripts(source, "level1", 1, [](std::string_view) {});
    coney::LevelObjectServices services(scripts.scripts(), scripts.flags(), nullptr);
    services.damageDone(1.0, 2.0); // no receiver: nothing
    std::vector<std::pair<double, double>> heard;
    services.setDamageReceiver([&heard](double human, double object) { heard.emplace_back(human, object); });
    services.damageDone(3.0, 4.0);
    CHECK(heard == std::vector<std::pair<double, double>>{{3.0, 4.0}});
}

TEST_CASE("a break-in lights the alarm strobe nearest the store's flag", "[gameplay][objects]") {
    const auto files = objectScripts();
    const coney::script::ScriptSource source =
        [&files](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
        const auto found = files.find(name);
        if (found == files.end()) {
            return coney::fail(coney::ErrorCode::NotFound, "no such script");
        }
        return found->second;
    };
    coney::LevelScripts scripts(source, "level1", 1, [](std::string_view) {});
    coney::LevelObjectServices services(scripts.scripts(), scripts.flags(), nullptr);
    // A store front (activity 14) at the origin, its alarm 3 m away and another strobe 30 m away.
    (void)scripts.flags().add(900.0, "storeFront", {0.0F, 0.0F, 0.0F}, 0.0F, 14);
    coney::effects::ParticleSystems particles;
    coney::effects::ParticleSystem* alarm = particles.spawn("part_strobe_red", coney::anim::Vec3{3.0F, 0.0F, 2.0F});
    REQUIRE(alarm != nullptr);
    const std::uint32_t alarmSerial = alarm->serial;
    REQUIRE(particles.spawn("part_strobe_red", coney::anim::Vec3{30.0F, 0.0F, 2.0F}) != nullptr);
    services.setParticles(&particles, {});
    // A break-in 20 m from the store finds no store: nothing lights.
    services.robStore(coney::anim::Vec3{20.0F, 0.0F, 0.0F}, 3);
    for (const coney::effects::ParticleSystem& system : particles.systems()) {
        CHECK_FALSE(system.emitting);
    }
    services.robStore(coney::anim::Vec3{2.0F, 2.0F, 0.0F}, 3);
    for (const coney::effects::ParticleSystem& system : particles.systems()) {
        CHECK(system.emitting == (system.serial == alarmSerial));
    }
    // The store is robbed by gang 3.
    const coney::world_objects::WorldFlag* store = scripts.flags().find(900.0);
    REQUIRE(store != nullptr);
    const auto group = static_cast<std::uint32_t>(store->kind2);
    CHECK((group & (1U << 16)) != 0);
    CHECK(((group >> 18) & 0x1fU) == 3);
}

TEST_CASE("the level's objects' services make a loose object a spawn record", "[gameplay][objects]") {
    const auto files = objectScripts();
    const coney::script::ScriptSource source =
        [&files](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
        const auto found = files.find(name);
        if (found == files.end()) {
            return coney::fail(coney::ErrorCode::NotFound, "no such script");
        }
        return found->second;
    };
    coney::LevelScripts scripts(source, "level1", 1, [](std::string_view) {});
    coney::LevelObjectServices services(scripts.scripts(), scripts.flags(), nullptr);
    // No records yet: nothing is made.
    CHECK(services.spawnObject("test_piece", {}, {}) == coney::world_objects::kNoObject);
    coney::world_objects::SpawnRecords records;
    services.setSpawnRecords(&records);
    const double piece = services.spawnObject("test_piece", coney::anim::Vec3{1, 2, 3}, coney::anim::Quat{});
    REQUIRE(piece != coney::world_objects::kNoObject);
    const coney::world_objects::SpawnRecord* record = records.find(piece);
    REQUIRE(record != nullptr);
    if (record == nullptr) {
        return;
    }
    CHECK(record->typeName == "test_piece");
    CHECK(record->position == std::array<float, 3>{1, 2, 3});
}
