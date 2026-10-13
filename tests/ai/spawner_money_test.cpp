// SPDX-License-Identifier: GPL-3.0-or-later
// The money a gang spawner's human starts with (docs/research/crimes.md#starting-money): the original rolls it in
// `Human_Init` for every human, `Spawner_MakeHuman` (`0x0016d860`) included, so a human the spawners make carries what
// a scripted one does, drawn in the same order from the game's random numbers. Each test runs the spawner's own path:
// the spawners (ai/spawners.h) over a world that makes the human by the scripts' `HuCreate`, as the play mode's does
// (gamemodes/gameplay_mode.cpp), over the scripted brains.
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/scripted_brains.h"
#include "ai/spawners.h"
#include "characters/starting_money.h"
#include "core/error.h"
#include "core/game_random.h"
#include "gui/global_strings.h"
#include "human/human.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/ai_fixtures.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using coney::script::Value;

namespace {

// A front-end host that ignores every request: spawning asks nothing of it.
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

// The character type the tests spawn, and the category its CfgChar record names.
constexpr int kType = 194;
constexpr int kCategory = 4;

// A CfgChar call for `type`: category (argument 3), the alternate-record byte (13), the object (15) and the drop chance
// (16); the other arguments 0.
std::vector<Value> cfgChar(int type, int category, const std::string& object, int dropChance) {
    std::vector<Value> args(16, Value(0.0));
    args[0] = Value(static_cast<double>(type));
    args[2] = Value(static_cast<double>(category));
    args[14] = Value(object);
    args[15] = Value(static_cast<double>(dropChance));
    return args;
}

// A CfgCharClassAttribs call: the class, its min and max, the unused chance, the no-object and bonus chances and the
// bonus.
std::vector<Value> cfgClass(int category, int min, int max, int noObject, int bonusChance, double bonus) {
    return {Value(static_cast<double>(category)),
            Value(static_cast<double>(min)),
            Value(static_cast<double>(max)),
            Value(0.1),
            Value(static_cast<double>(noObject)),
            Value(static_cast<double>(bonusChance)),
            Value(bonus)};
}

// A level whose spawners make humans by the scripts' HuCreate over the scripted brains; every human it makes gets a
// brain of the synthetic scene, as the play mode's cast does.
struct SpawnLevel final : public coney::ai::SpawnerWorld {
    coney::test::AiScene scene;
    coney::world_objects::WorldFlags flags;
    std::unique_ptr<coney::ai::ScriptedBrains> scripted;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::script::BindingContext context;
    std::unique_ptr<coney::script::ScriptSystem> scripts;
    coney::ai::Spawners spawners;

    SpawnLevel() {
        flags.createPool(4);
        scripted = std::make_unique<coney::ai::ScriptedBrains>(scene.brains, flags);
        scripted->bind(1.0, scene.player());
        scripted->setPlayer(&scene.player());
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.recorded = &recorded;
        context.humans = &humans;
        context.ai = scripted.get();
        scripts = std::make_unique<coney::script::ScriptSystem>(
            [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
            },
            [this](coney::script::ScriptSystem& system, coney::script::LuaVm& vm) {
                coney::script::installBindings(system, vm, context);
            },
            coney::script::ScriptSystem::Log{});
        scripts->create();
        scripted->setScripts(scripts.get());
        // The level has loaded: a human the scripts make now gets its brain at once.
        scripted->release([this](const coney::HumanCreation& human) -> coney::ai::Brain* {
            const std::array<float, 3> at = human.position.value_or(std::array<float, 3>{});
            return &scene.add({at[0], at[1], at[2]}, human.headingDegrees);
        });
    }

    // Records a configuration call as the level's boot scripts do.
    void configure(std::string_view binding, const std::vector<Value>& args) { recorded.add(binding, args); }

    [[nodiscard]] std::optional<coney::anim::Vec3> playerPosition() const override { return std::nullopt; }
    [[nodiscard]] std::optional<coney::anim::Vec3> outOfSight(float /*value*/, int /*gang*/) override {
        return std::nullopt;
    }
    [[nodiscard]] bool alive(double handle) const override {
        const auto found = scripted->bound().find(handle);
        return found != scripted->bound().end() && found->second->human().alive();
    }
    [[nodiscard]] bool seen(coney::anim::Vec3 /*centre*/, float /*radius*/) const override { return false; }
    // A copy of the play mode's ScriptSpawnerWorld::spawn (gamemodes/gameplay_mode.cpp, in an anonymous namespace, so
    // the test cannot use the class): HuCreate(name, type, {x, y, z}, heading, model, 0, gang). It cannot see that
    // function stop calling HuCreate; the disc test (tests/platform/disc_spawner_money_test.cpp) runs the real one but
    // asserts only the street's civilians and bums, which the scripts make by HuCreate themselves.
    double spawn(const coney::ai::SpawnRequest& request) override {
        auto position = std::make_shared<coney::script::Table>();
        for (std::size_t i = 0; i < request.position.size(); ++i) {
            REQUIRE(position->set(Value(static_cast<double>(i + 1)), Value(request.position.at(i))).has_value());
        }
        const std::array<Value, 7> args{Value(request.name),
                                        Value(static_cast<double>(request.type)),
                                        Value(std::move(position)),
                                        Value(static_cast<double>(request.heading)),
                                        Value(request.model),
                                        Value(0.0),
                                        Value(static_cast<double>(request.gang))};
        coney::script::LuaVm& vm = scripts->vm();
        const auto made = vm.call(vm.global("HuCreate"), args);
        REQUIRE(made.has_value());
        return made->empty() ? 0.0 : made->front().number().value_or(0.0);
    }
    void spawned(std::string_view /*callback*/, double /*handle*/, int /*gang*/,
                 std::string_view /*spawner*/) override {}

    // The money of the human with `handle`, which must have a brain.
    [[nodiscard]] int moneyOf(double handle) const {
        const auto found = scripted->bound().find(handle);
        REQUIRE(found != scripted->bound().end());
        return found->second->human().script().money;
    }
};

// A spawner of gang 3 making `kType` at once, one human in all.
coney::script::SpawnerCall oneHuman() {
    coney::script::SpawnerCall call;
    call.gang = 3;
    call.name = "WS";
    call.types = {kType, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    call.model = "warr_sw";
    call.position = {10.0F, 20.0F, 0.0F};
    call.total = 1;
    call.maxConcurrent = 1;
    call.state = static_cast<int>(coney::ai::SpawnerState::On);
    return call;
}

} // namespace

TEST_CASE("a spawner's human of a $5-$20 class with no object holds $5-$20", "[ai][spawners]") {
    SpawnLevel level;
    level.configure("CfgChar", cfgChar(kType, kCategory, "none", -1));
    level.configure("CfgCharClassAttribs", cfgClass(kCategory, 5, 20, 100, 0, 1.0));
    level.spawners.add(oneHuman());
    level.spawners.update(0, level);
    REQUIRE(level.humans.all().size() == 1);
    const double handle = level.humans.all().front().handle;
    const int money = level.moneyOf(handle);
    CHECK(money >= 5);
    CHECK(money <= 20);
}

TEST_CASE("a spawner's human rolls its money by the class's draws, in their order, from the game's stream",
          "[ai][spawners]") {
    // The street civilian's class: a bonus roll of 1 in 100 that multiplies by 5. The reference rolls the same carry
    // from the same stream, the roll HuCreate makes for a scripted human.
    const coney::characters::CarryConfig config{
        .category = kCategory, .altRecord = false, .dropChance = -1, .object = "none"};
    const coney::characters::ClassMoney cls{.min = 5, .max = 20, .noObject = 100, .bonusChance = 1, .bonus = 5.0F};
    for (const std::uint32_t seed : {0U, 17U, 300U, 1000U}) {
        SpawnLevel spawner;
        spawner.configure("CfgChar", cfgChar(kType, kCategory, "none", -1));
        spawner.configure("CfgCharClassAttribs", cfgClass(kCategory, 5, 20, 100, 1, 5.0));
        spawner.state.random.startAt(seed);
        spawner.spawners.add(oneHuman());
        spawner.spawners.update(0, spawner);
        REQUIRE(spawner.humans.all().size() == 1);
        const int money = spawner.moneyOf(spawner.humans.all().front().handle);
        coney::GameRandom reference;
        reference.startAt(seed);
        const coney::characters::StartingCarry expected = coney::characters::rollStartingCarry(config, cls, reference);
        CHECK(money == expected.money);
        // The same draws, no more and no fewer.
        CHECK(spawner.state.random.index() == reference.index());
    }
}
