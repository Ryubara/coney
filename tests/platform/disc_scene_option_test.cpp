// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `--scene` (docs/guides/running-play-level.md#playing-a-level) plays a scene
// in a level other than level99, the case of issue 446: the level starts as `coney --play-level level2 --scene l2_c1`
// starts it, the scene starts with its camera current and puppets exist for its roles, and the stage hands each posed
// puppet's own mesh to the draw its caller gives it, one call per posed puppet. The crash itself only a window shows,
// so these tests do not show it. They run only when the environment variable CONEY_DISC names the disc and skip
// otherwise; they print counts only (LEGAL.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "characters/character_data.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/scene_stage.h"
#include "scenes/scene_host.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "world_objects/spawn_records.h"

namespace {

// The scene of issue 446, in the level the issue played.
constexpr std::string_view kLevel = "level2";
constexpr std::string_view kScene = "l2_c1";
// Frames of play to give the scene before looking: past its first characters.
constexpr std::uint64_t kLookAt = 120;

} // namespace

TEST_CASE("the disc's level2: --scene l2_c1 starts with its camera current and puppets for its roles",
          "[disc][scenes][options]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "");
    // main's first-level set-up for `--scene`: the scene plays at once, once the level is loaded.
    bool played = false;
    std::string failure;
    run.session().setFirstLevelSetup([&](coney::platform::PlayLevelMode& mode) -> std::expected<void, coney::Error> {
        auto result = mode.playScene(kScene);
        played = result.has_value();
        if (!result) {
            failure = result.error().message;
            return std::unexpected(std::move(result.error()));
        }
        return {};
    });
    run.session().startAtLevel(kLevel, 1);
    REQUIRE(run.runUntilPlay());
    INFO(failure);
    CHECK(played);
    run.run(kLookAt);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    CHECK(play->stage().cameraActive());
    // The scene's roles other than player 1's are puppets (l2_c1 has nine roles).
    const std::size_t puppets = play->stage().puppetCount();
    std::printf("--scene %s in %s: scene started %s, %zu puppets after %llu frames of play\n",
                std::string(kScene).c_str(), std::string(kLevel).c_str(), played ? "yes" : "no", puppets,
                static_cast<unsigned long long>(run.playFrames()));
    CHECK(puppets > 0);
}

TEST_CASE("a scene's posed puppets are handed to the caller's draw, each its own mesh, once each",
          "[disc][scenes][options]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    // Expected: scenes.md#coneys-implementation, Coney's own stage: a bound human the play mode does not draw is a
    // puppet. This stage draws every bound human as the player's own model, loaded from the disc.
    const coney::io::Wad& discWad = *wad;
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    coney::platform::SceneStage stage(
        [&discWad, &table](std::string_view model) -> std::expected<coney::platform::StageCharacter, coney::Error> {
            auto character = coney::human::PlayerCharacter::load(discWad, table, model);
            if (!character) {
                return std::unexpected(std::move(character.error()));
            }
            return coney::platform::StageCharacter{.character = std::move(*character), .dictionaries = {}};
        },
        [](double, std::size_t, std::string_view) { return std::string(coney::human::kPlayerModel); },
        [](std::string_view) {});
    // Two humans join; both become puppets, but only the one the scene has posed is drawn.
    const coney::scenes::ScenePose start;
    stage.humanJoin(1000.0, 0, 0, start, 0);
    stage.humanJoin(1001.0, 0, 1, start, 0);
    REQUIRE(stage.humanReady(1000.0));
    REQUIRE(stage.humanReady(1001.0));
    REQUIRE(stage.puppetCount() == 2);
    // The meshes each draw hands over: one per posed puppet, each with its atomics, none twice.
    std::vector<const coney::platform::CharacterMesh*> drawn;
    const auto record = [&drawn](coney::platform::CharacterMesh& mesh) {
        CHECK(mesh.atomic() != nullptr);
        CHECK(mesh.bloodAtomic() != nullptr);
        drawn.push_back(&mesh);
    };
    stage.drawPuppets(record);
    CHECK(drawn.empty());
    stage.humanPose(1000.0, coney::scenes::RoleFrame{.role = 0, .pose = {}, .feet = {}, .heading = 0.0F});
    stage.drawPuppets(record);
    REQUIRE(drawn.size() == 1);
    const coney::platform::CharacterMesh* first = drawn[0];
    stage.humanPose(1001.0, coney::scenes::RoleFrame{.role = 1, .pose = {}, .feet = {}, .heading = 0.0F});
    drawn.clear();
    stage.drawPuppets(record);
    REQUIRE(drawn.size() == 2);
    CHECK(drawn[0] != drawn[1]);
    CHECK((drawn[0] == first || drawn[1] == first));
    std::printf("scene puppets: 2 joined, 0 then 1 then 2 distinct meshes drawn as they were posed\n");
}

namespace {

// The preview's stand-ins take handle 1000 + their role (play_level_scene.cpp); `l99_c1`'s Ash is role 7.
constexpr double kAshStandIn = 1007.0;
// A level99 checkpoint whose start plays neither l99_c1 nor Coney_Sub2 itself (its scripts play l99_c2 and the wonder
// wheel), so the preview's bindings are the only ones in those scenes.
constexpr int kQuietCheckpoint = 2;

// Starts level99 at kQuietCheckpoint with `scene` played through `--scene`'s set-up; false when it did not start.
bool startPreview(coney::test::DiscSession& run, std::string_view scene, std::string& failure) {
    bool played = false;
    run.session().setFirstLevelSetup([&](coney::platform::PlayLevelMode& mode) -> std::expected<void, coney::Error> {
        auto result = mode.playScene(scene);
        played = result.has_value();
        if (!result) {
            failure = result.error().message;
            return std::unexpected(std::move(result.error()));
        }
        return {};
    });
    run.session().startAtLevel("level99", kQuietCheckpoint);
    return run.runUntilPlay() && played;
}

} // namespace

TEST_CASE("the disc's level99: --scene l99_c1 draws Ash as the type its script binds, warr_ty_cv",
          "[disc][scenes][options]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "");
    std::string failure;
    const bool played = startPreview(run, "l99_c1", failure);
    INFO(failure);
    REQUIRE(played);
    run.run(kLookAt);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    // Expected: scenes.md#cast-runtime, confirmed (runtime)
    // docs/research/scenes.md#cast-runtime: role 7 (`warrashcv`) is bound to Ash, character type 40, `warr_ty_cv`;
    // role 1 (`warrve`) to Vermin, `warr_ve_gen`; role 3 to nobody, so it is no puppet.
    const std::string ash = play->stage().modelOf(kAshStandIn);
    const std::string vermin = play->stage().modelOf(1001.0);
    std::printf("--scene l99_c1 in level99: Ash drawn as %s, Vermin as %s, role 3 bound %s, %zu puppets\n", ash.c_str(),
                vermin.c_str(), play->stage().holds(1003.0) ? "yes" : "no", play->stage().puppetCount());
    CHECK(ash == "warr_ty_cv");
    CHECK(vermin == "warr_ve_gen");
    CHECK_FALSE(play->stage().holds(1003.0));
}

TEST_CASE("the disc's level99: --scene Coney_Sub2 moves dyn_s_train_med_full cars in its slots",
          "[disc][scenes][options]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "");
    std::string failure;
    std::set<double> before;
    bool played = false;
    run.session().setFirstLevelSetup([&](coney::platform::PlayLevelMode& mode) -> std::expected<void, coney::Error> {
        for (const coney::world_objects::SpawnRecord& record : run.session().flow().spawnRecords().all()) {
            before.insert(record.handle);
        }
        auto result = mode.playScene("Coney_Sub2");
        played = result.has_value();
        if (!result) {
            failure = result.error().message;
            return std::unexpected(std::move(result.error()));
        }
        return {};
    });
    run.session().startAtLevel("level99", kQuietCheckpoint);
    REQUIRE(run.runUntilPlay());
    INFO(failure);
    REQUIRE(played);
    // The preview's cars: the records of the bound type its set-up added, and where each stands.
    const auto cars = [&run, &before] {
        std::map<double, std::array<float, 3>> found;
        for (const coney::world_objects::SpawnRecord& record : run.session().flow().spawnRecords().all()) {
            if (!before.contains(record.handle) && record.typeName == "dyn_s_train_med_full" && record.pinned) {
                found.emplace(record.handle, record.position);
            }
        }
        return found;
    };
    run.run(kLookAt);
    const std::map<double, std::array<float, 3>> early = cars();
    run.run(kLookAt);
    const std::map<double, std::array<float, 3>> late = cars();
    // Expected: scenes.md#cast-runtime, confirmed (runtime)
    // docs/research/scenes.md#cast-runtime: the script binds slots 0-2 and 4-6 to objects of type
    // `dyn_s_train_med_full`; slot 3 (`train_sound`) to an object with no world-object type, which the preview leaves.
    // A new record stands at the origin until the scene's object moves place it (SceneStage::objectPose), so a car
    // away from the origin, and one that moves between the two looks, is one a slot of the playing scene holds.
    std::size_t placed = 0;
    std::size_t moved = 0;
    for (const auto& [handle, at] : late) {
        placed += at != std::array<float, 3>{} ? 1 : 0;
        const auto then = early.find(handle);
        moved += then != early.end() && then->second != at ? 1 : 0;
    }
    std::printf("--scene Coney_Sub2 in level99: %zu new pinned dyn_s_train_med_full records, %zu placed by the scene, "
                "%zu moved between frames %llu and %llu\n",
                late.size(), placed, moved, static_cast<unsigned long long>(kLookAt),
                static_cast<unsigned long long>(2 * kLookAt));
    CHECK(late.size() == 6);
    CHECK(placed == 6);
    CHECK(moved > 0);
}
