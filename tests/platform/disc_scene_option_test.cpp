// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `--scene` (docs/guides/running-play-level.md#playing-a-level) plays a scene
// in a level other than level99, the case of issue 446: the level starts as `coney --play-level level2 --scene l2_c1`
// starts it, the scene starts with its camera current and puppets exist for its roles, and the stage hands each posed
// puppet's own mesh to the draw its caller gives it, one call per posed puppet. The crash itself only a window shows,
// so these tests do not show it. They run only when the environment variable CONEY_DISC names the disc and skip
// otherwise; they print counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
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
    // A stage that draws every bound human as the player's own model, loaded from the disc.
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
