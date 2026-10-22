// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: the Entity inspector's freeze of a human other than the player, and the offset
// that puts its debug shapes on the drawn model, in the sandbox's fight yard (assets/sandbox/combat.layout), with
// level99's configuration as `--play-level sandbox:combat` takes it. Headless, on the fixed step with no clock. It runs
// only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "debug/entity_inspect.h"
#include "debug/play_controls.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "human/victim.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sandbox_play_scenery.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"
#include "support/test_paths.h"

namespace {

// The ids of the inspector's humans other than the player, in the list's order.
std::vector<coney::debug::EntityId> otherHumans(coney::platform::PlayLevelMode& mode) {
    const coney::platform::PlayLevelControls controls(&mode);
    std::vector<coney::debug::EntityId> ids;
    REQUIRE(controls.inspect() != nullptr);
    for (const coney::debug::EntityRef& entity : controls.inspect()->entities()) {
        if (entity.kind == coney::debug::EntityKind::Human) {
            ids.push_back(entity.id);
        }
    }
    return ids;
}

// Where entity `id` stands now; the origin when it is not listed.
coney::anim::Vec3 positionOf(coney::platform::PlayLevelMode& mode, coney::debug::EntityId id) {
    const coney::platform::PlayLevelControls controls(&mode);
    const std::vector<coney::debug::EntityRef> entities = controls.inspect()->entities();
    const coney::debug::EntityRef* entity = coney::debug::findEntity(entities, id);
    return entity != nullptr ? entity->position : coney::anim::Vec3{};
}

// The combat sandbox on the disc's level99 configuration, started and stepped on the fixed timestep.
struct Yard {
    std::optional<coney::io::Wad> wad;
    std::unique_ptr<coney::platform::RenderEngine> engine;
    std::unique_ptr<coney::platform::PlayLevelMode> mode;
    coney::GameModeStack modes;
    coney::GameTimer timer;

    // Steps the world `count` times.
    void run(int count) { (void)modes.runUntilEmpty(timer, {}, count); }
};

// Opens the yard from CONEY_DISC; false (and `yard` unset) when there is no disc, so the case can skip.
bool openYard(Yard& yard) {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return false;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    yard.wad = std::move(*wad);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    yard.engine = std::move(*engine);

    // The sandbox's setup as main makes it: level99's configuration.
    const coney::LevelScriptRun run =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*yard.wad), "level99", 1, {});
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(run.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(run.recorded);
    auto world = coney::sandbox::SandboxWorld::load(coney::test::assetsDir() / "sandbox", "combat");
    REQUIRE(world.has_value());
    auto made = coney::platform::createPlayInSandbox(
        *yard.engine, *yard.wad, std::move(*world), std::nullopt, [](std::string_view) {}, setup);
    REQUIRE(made.has_value());
    yard.mode = std::move(*made);
    yard.timer.setFixedStep(true);
    yard.modes.push(*yard.mode);
    yard.run(5);
    return true;
}

} // namespace

TEST_CASE("a frozen AI human stays where it is while the rest of the world runs", "[disc][debug]") {
    Yard yard;
    if (!openYard(yard)) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::PlayLevelMode& mode = *yard.mode;
    coney::platform::PlayLevelControls controls(&mode);

    // Two fighters, to the player's left and right ahead of him, both facing him.
    const std::vector<coney::debug::EntityId> before = otherHumans(mode);
    const coney::anim::Vec3 ahead = coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 4.0F);
    REQUIRE(
        mode.spawnFighter(coney::anim::Vec3{ahead.x - 1.5F, ahead.y, ahead.z}, mode.playerHeadingDegrees() + 180.0F));
    REQUIRE(
        mode.spawnFighter(coney::anim::Vec3{ahead.x + 1.5F, ahead.y, ahead.z}, mode.playerHeadingDegrees() + 180.0F));
    const std::vector<coney::debug::EntityId> after = otherHumans(mode);
    REQUIRE(after.size() == before.size() + 2);
    const coney::debug::EntityId frozen = after[after.size() - 2];
    const coney::debug::EntityId loose = after.back();

    // Only humans can be frozen, and nothing is frozen at first.
    CHECK(controls.canFreezeEntity(frozen));
    CHECK_FALSE(controls.entityFrozen(frozen));
    CHECK_FALSE(controls.canFreezeEntity(coney::debug::kNoEntity));
    for (const coney::debug::EntityRef& entity : controls.inspect()->entities()) {
        if (entity.kind == coney::debug::EntityKind::Object) {
            CHECK_FALSE(controls.canFreezeEntity(entity.id));
        }
    }
    controls.setEntityFrozen(frozen, true);
    CHECK(controls.entityFrozen(frozen));
    CHECK_FALSE(controls.entityFrozen(loose));

    // The world runs: the loose fighter, which fights the player, moves; the frozen one does not move at all.
    const coney::anim::Vec3 frozenAt = positionOf(mode, frozen);
    const coney::anim::Vec3 freeAt = positionOf(mode, loose);
    yard.run(90);
    const coney::anim::Vec3 frozenNow = positionOf(mode, frozen);
    const coney::anim::Vec3 freeNow = positionOf(mode, loose);
    const float freeMoved = coney::anim::distance(freeAt, freeNow);
    std::printf("inspector freeze: the frozen fighter moved %.4f m, the loose one %.2f m in 90 steps\n",
                coney::anim::distance(frozenAt, frozenNow), freeMoved);
    CHECK(frozenNow.x == frozenAt.x);
    CHECK(frozenNow.y == frozenAt.y);
    CHECK(frozenNow.z == frozenAt.z);
    CHECK(freeMoved > 0.1F);

    // Let go, and it moves again.
    controls.setEntityFrozen(frozen, false);
    CHECK_FALSE(controls.entityFrozen(frozen));
    yard.run(90);
    CHECK(coney::anim::distance(frozenAt, positionOf(mode, frozen)) > 0.1F);
}

TEST_CASE("a frozen sandbox target takes no step, and the player's freeze reads through the entity", "[disc][debug]") {
    Yard yard;
    if (!openYard(yard)) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::PlayLevelMode& mode = *yard.mode;
    coney::platform::PlayLevelControls controls(&mode);
    REQUIRE(!mode.targets().empty());
    // The first sandbox target's entity.
    coney::debug::EntityId targetId = coney::debug::kNoEntity;
    for (const coney::debug::EntityRef& entity : controls.inspect()->entities()) {
        if (entity.kind == coney::debug::EntityKind::Human && entity.label.starts_with("target")) {
            targetId = entity.id;
            break;
        }
    }
    REQUIRE(targetId != coney::debug::kNoEntity);
    coney::human::TargetHuman& target = *mode.targets().front();

    // A hit waits for a frozen target (its step is not run) and lands on the first step after it is let go.
    coney::human::IncomingHit hit;
    hit.damage = 5;
    hit.attacker = mode.playerFeet();
    CHECK(controls.canFreezeEntity(targetId));
    controls.setEntityFrozen(targetId, true);
    CHECK(controls.entityFrozen(targetId));
    target.hit(hit);
    yard.run(30);
    CHECK(target.hitsTaken() == 0);
    controls.setEntityFrozen(targetId, false);
    CHECK_FALSE(controls.entityFrozen(targetId));
    yard.run(2);
    std::printf("inspector freeze: a target hit while frozen took %d hit(s) after being let go\n", target.hitsTaken());
    CHECK(target.hitsTaken() == 1);

    // The player's freeze is the Player page's: one flag, read through the entity.
    constexpr coney::debug::EntityId kPlayer = 1;
    CHECK(controls.canFreezeEntity(kPlayer));
    CHECK_FALSE(controls.entityFrozen(kPlayer));
    controls.setEntityFrozen(kPlayer, true);
    CHECK(mode.playerFrozen());
    mode.setPlayerFrozen(false);
    CHECK_FALSE(controls.entityFrozen(kPlayer));
}

TEST_CASE("the debug shapes' offset puts them on the drawn model between two steps", "[disc][debug]") {
    Yard yard;
    if (!openYard(yard)) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::PlayLevelMode& mode = *yard.mode;
    coney::platform::PlayLevelControls controls(&mode);
    const coney::anim::Vec3 ahead = coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 4.0F);
    REQUIRE(mode.spawnFighter(ahead, mode.playerHeadingDegrees() + 180.0F));
    const std::vector<coney::debug::EntityId> humans = otherHumans(mode);
    REQUIRE(!humans.empty());
    const coney::debug::EntityId fighter = humans.back();
    yard.run(60);
    // Drawn at the end of the last step: no offset. At its start: back to the step before. Between: in proportion.
    const coney::anim::Vec3 atStart = coney::platform::PlayLevelControls::drawnOffsetOf(mode, fighter, 0.0F);
    const coney::anim::Vec3 atHalf = coney::platform::PlayLevelControls::drawnOffsetOf(mode, fighter, 0.5F);
    const coney::anim::Vec3 atEnd = coney::platform::PlayLevelControls::drawnOffsetOf(mode, fighter, 1.0F);
    std::printf("shape offset: %.4f m back at alpha 0 (the step before)\n", coney::anim::length(atStart));
    CHECK(atEnd.x == Catch::Approx(0.0F).margin(1e-5F));
    CHECK(atEnd.y == Catch::Approx(0.0F).margin(1e-5F));
    CHECK(atHalf.x == Catch::Approx(atStart.x * 0.5F).margin(1e-5F));
    CHECK(atHalf.y == Catch::Approx(atStart.y * 0.5F).margin(1e-5F));
    CHECK(coney::anim::length(atStart) > 0.0F);
    // No entity has no model to follow.
    CHECK(coney::anim::length(coney::platform::PlayLevelControls::drawnOffsetOf(mode, coney::debug::kNoEntity, 0.5F)) ==
          0.0F);
}
