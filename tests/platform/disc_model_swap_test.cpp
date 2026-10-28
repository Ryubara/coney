// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the Entity inspector's Model edit (docs/guides/debug-menu.md
// #entity-inspector) gives a human another model in place: in the sandbox's fight yard (assets/sandbox/combat.layout)
// with level99's configuration, as `--play-level sandbox:combat` takes it, player 1, an AI fighter of the player's side
// and a sandbox target each take a model the configuration names, and keep their gang, sides, enemies, brain and goals,
// meters, place, heading and clip; only the drawn mesh and its skeleton change. Headless, on the fixed step with no
// clock. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts
// only (LEGAL.md).

#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/fighter_side.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "debug/entity_edit.h"
#include "debug/entity_inspect.h"
#include "debug/play_controls.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sandbox_play_scenery.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"
#include "support/test_paths.h"

using coney::debug::EditOp;
using coney::debug::EntityCommand;
using coney::debug::EntityId;

namespace {

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

// The watch value `name` of group `group` of entity `id`; empty when it has none.
std::string watched(const coney::platform::PlayLevelControls& controls, EntityId id, std::string_view group,
                    std::string_view name) {
    const std::vector<coney::debug::WatchGroup> groups = controls.inspect()->describe(id);
    return coney::debug::watchValue(groups, group, name).value_or("");
}

// What a swap must leave alone, read through the inspector: the place, the heading, the meters and the clip.
struct Kept {
    std::string feet;
    std::string heading;
    std::string health;
    std::string stamina;
    std::string power;
    std::string rage;
    std::string clip;
    friend bool operator==(const Kept&, const Kept&) = default;
};

// Entity `id`'s Kept values now.
Kept keptOf(const coney::platform::PlayLevelControls& controls, EntityId id) {
    return Kept{.feet = watched(controls, id, "Transform", "Feet"),
                .heading = watched(controls, id, "Transform", "Heading"),
                .health = watched(controls, id, "Meters", "Health"),
                .stamina = watched(controls, id, "Meters", "Stamina"),
                .power = watched(controls, id, "Meters", "Power"),
                .rage = watched(controls, id, "Meters", "Rage"),
                .clip = watched(controls, id, "Animation", "Clip")};
}

// The first model of `models` that is not `current`; empty when there is none.
std::string otherModel(const std::vector<std::string>& models, const std::string& current) {
    for (const std::string& model : models) {
        if (model != current) {
            return model;
        }
    }
    return {};
}

// Gives entity `id` the model `model` through the edit target, as the input gate applies the page's edit, and checks
// that the inspector names the new model while the place, heading, meters and clip stay.
void swapAndCheck(coney::platform::PlayLevelControls& controls, EntityId id, const std::string& model) {
    const Kept before = keptOf(controls, id);
    const std::string vertices = watched(controls, id, "Mesh", "Vertices");
    const auto applied = controls.applyEdit(EntityCommand{
        .entity = id, .op = EditOp::Model, .values = {static_cast<double>(coney::debug::modelId(model)), 0.0, 0.0}});
    INFO(id);
    REQUIRE(applied.has_value());
    CHECK(keptOf(controls, id) == before);
    CHECK(watched(controls, id, "Identity", "Model") == model);
    CHECK(controls.currentValue(id, EditOp::Model) == static_cast<double>(coney::debug::modelId(model)));
    std::printf("model swap: entity %llu to a model of %s vertices (was %s)\n", static_cast<unsigned long long>(id),
                watched(controls, id, "Mesh", "Vertices").c_str(), vertices.c_str());
}

} // namespace

TEST_CASE("a model swap gives player 1, an AI fighter and a sandbox target another model and keeps the rest",
          "[disc][debug][entity-editor]") {
    // Expected: human-model-change.md#keeps, confirmed (code) (what the late fill-in leaves; the swap is Coney's own)
    Yard yard;
    if (!openYard(yard)) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::PlayLevelMode& mode = *yard.mode;
    coney::platform::PlayLevelControls controls(&mode);
    const std::vector<std::string> models = controls.models();
    REQUIRE(models.size() >= 2);

    // An ally fighter beside the player, of the first model, so its sides and enemies are set.
    const coney::anim::Vec3 ahead = coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 3.0F);
    REQUIRE(mode.spawnFighterAs(ahead, mode.playerHeadingDegrees(), models.front(), coney::ai::FighterSide::Ally));
    yard.run(10);
    coney::ai::AiHumans& fighters = mode.fighters();
    const std::size_t index = fighters.humans().size() - 1;
    const EntityId fighterId = coney::platform::PlayLevelControls::kInspectAiBase + index;
    const coney::human::Human* fighterHuman = fighters.humans()[index].human.get();
    const coney::ai::Brain* brain = fighters.brains().find(*fighterHuman);
    REQUIRE(brain != nullptr);
    const coney::ai::Gang* gang = brain->gang();
    const coney::ai::BrainType brainType = brain->type();
    const std::size_t goals = brain->goalCount();
    const std::vector<coney::ai::Brain*> enemies = brain->enemies();
    const coney::ai::FighterSide side = fighters.humans()[index].side;
    const coney::ai::Gang* playerGang = fighters.playerBrain().gang();

    // The AI fighter: the same human and brain, of the same gang and side, with the same enemies and goals.
    swapAndCheck(controls, fighterId, otherModel(models, models.front()));
    CHECK(fighters.humans()[index].human.get() == fighterHuman);
    CHECK(fighters.brains().find(*fighterHuman) == brain);
    CHECK(brain->gang() == gang);
    CHECK(brain->type() == brainType);
    CHECK(brain->goalCount() == goals);
    CHECK(brain->enemies() == enemies);
    CHECK(fighters.humans()[index].side == side);

    // Player 1: the same human, brain, gang and type; the mode draws him as the new model.
    const coney::human::Human* player = &mode.player().human();
    const int type = mode.playerType();
    const std::string playerModel = otherModel(models, mode.model());
    swapAndCheck(controls, 1, playerModel);
    CHECK(&mode.player().human() == player);
    CHECK(fighters.playerBrain().gang() == playerGang);
    CHECK(mode.playerType() == type);
    CHECK(mode.model() == playerModel);

    // A sandbox target.
    REQUIRE(!mode.targets().empty());
    const coney::human::TargetHuman* target = mode.targets().front();
    swapAndCheck(controls, coney::platform::PlayLevelControls::kInspectTargetBase, models.back());
    CHECK(mode.targets().front() == target);

    // The world runs on with the new models: everyone still in it, nobody's brain changed.
    yard.run(60);
    CHECK_FALSE(fighters.humans()[index].removed);
    CHECK(fighters.brains().find(*fighterHuman) == brain);
    CHECK(brain->gang() == gang);

    // A model the configuration does not name is refused, changing nothing.
    const auto refused = controls.applyEdit(EntityCommand{.entity = fighterId, .op = EditOp::Model, .values = {1.0}});
    CHECK_FALSE(refused.has_value());
    CHECK(watched(controls, fighterId, "Identity", "Model") == otherModel(models, models.front()));
}
