// SPDX-License-Identifier: GPL-3.0-or-later
// The later missions' character bindings from Lua to the level's humans (scripting/character_bindings.h,
// ai/scripted_humans.h): each binding called by name in a script state whose AI host is the scripted brains over a
// synthetic scene, then the humans and brains checked. Handles: 1 the player, 2 and up the AI humans the tests add.
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/scripted_brains.h"
#include "core/error.h"
#include "gui/global_strings.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/ai_fixtures.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/spawn_records.h"

using coney::ai::Brain;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;
namespace flag = coney::human::flag;

namespace {

// A front-end host that ignores every request: these bindings ask nothing of it.
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

// A synthetic scene whose brains a script state's bindings drive through the scripted brains.
struct Level {
    coney::test::AiScene scene;
    coney::world_objects::WorldFlags flags;
    coney::world_objects::SpawnRecords records;
    std::unique_ptr<coney::ai::ScriptedBrains> scripted;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::BindingContext context;
    std::unique_ptr<ScriptSystem> scripts;

    Level() {
        flags.createPool(4);
        scripted = std::make_unique<coney::ai::ScriptedBrains>(scene.brains, flags);
        scripted->bind(1.0, scene.player());
        scripted->setPlayer(&scene.player());
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.ai = scripted.get();
        context.spawnRecords = &records;
        scripts = std::make_unique<ScriptSystem>(
            [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
            },
            [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); },
            ScriptSystem::Log{});
        scripts->create();
        scripted->setScripts(scripts.get());
    }

    // An AI human at `feet`, bound to the next handle.
    Brain& add(coney::anim::Vec3 feet) {
        Brain& brain = scene.add(feet, 0.0F);
        scripted->bind(brain.handle(), brain);
        return brain;
    }

    // Calls the binding `name` with `args`; REQUIREs success and returns its first result (nil for none).
    Value call(std::string_view name, const std::vector<Value>& args) {
        auto result = scripts->vm().call(scripts->vm().global(name), args);
        REQUIRE(result.has_value());
        return result->empty() ? Value() : result->front();
    }
};

} // namespace

TEST_CASE("the killer, double-damage, rubber-neck and grab-block switches set and clear their bits", "[ai][scripted]") {
    Level level;
    const Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    const coney::human::Human& human = thug.human();
    level.call("HuSetKillerMode", {Value(2.0), Value(1.0)});
    level.call("HuSetDoubleDamage", {Value(2.0), Value(1.0)});
    level.call("HuRubberNeck", {Value(2.0), Value(1.0)});
    level.call("HuBlockRearGrab", {Value(2.0), Value(1.0)});
    CHECK(human.hasFlag(flag::kKiller));
    CHECK(human.hasFlag(flag::kDoubleDamage));
    CHECK(human.hasFlag(flag::kRubberNeck));
    CHECK(human.hasFlag(flag::kBlockRearGrab));
    CHECK_FALSE(human.hasFlag(flag::kBlockFrontGrab));
    // HuBlockGrab sets both grab bits, and clears both.
    level.call("HuBlockGrab", {Value(2.0), Value(1.0)});
    CHECK(human.hasFlag(flag::kBlockFrontGrab));
    CHECK(human.hasFlag(flag::kBlockRearGrab));
    level.call("HuBlockGrab", {Value(2.0), Value()});
    CHECK_FALSE(human.hasFlag(flag::kBlockFrontGrab));
    CHECK_FALSE(human.hasFlag(flag::kBlockRearGrab));
    // nil and 0 clear; a missing argument clears.
    level.call("HuSetKillerMode", {Value(2.0), Value(0.0)});
    level.call("HuSetDoubleDamage", {Value(2.0), Value()});
    level.call("HuRubberNeck", {Value(2.0)});
    CHECK_FALSE(human.hasFlag(flag::kKiller));
    CHECK_FALSE(human.hasFlag(flag::kDoubleDamage));
    CHECK_FALSE(human.hasFlag(flag::kRubberNeck));
    // A handle that names no human does nothing.
    level.call("HuSetKillerMode", {Value(99.0), Value(1.0)});
}

TEST_CASE("HuSetWoundable takes only exactly 1 as on", "[ai][scripted]") {
    Level level;
    const Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    level.call("HuSetWoundable", {Value(2.0), Value(2.0)});
    CHECK_FALSE(thug.human().hasFlag(flag::kWoundable));
    level.call("HuSetWoundable", {Value(2.0), Value(1.0)});
    CHECK(thug.human().hasFlag(flag::kWoundable));
    level.call("HuSetWoundable", {Value(2.0), Value()});
    CHECK_FALSE(thug.human().hasFlag(flag::kWoundable));
}

TEST_CASE("HuSetHearRange and HuSetHelpHearRange set the brain's noise and help-call ranges", "[ai][scripted]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    CHECK(thug.senses().hearRange == coney::ai::kDefaultHearRange);
    CHECK(thug.senses().helpHearRange == coney::ai::kDefaultHelpHearRange);
    level.call("HuSetHearRange", {Value(2.0), Value(3.0)});
    level.call("HuSetHelpHearRange", {Value(2.0), Value(7.5)});
    CHECK(thug.senses().hearRange == 3.0F);
    CHECK(thug.senses().helpHearRange == 7.5F);
    // 0 makes it deaf: stored as given.
    level.call("HuSetHearRange", {Value(2.0), Value(0.0)});
    CHECK(thug.senses().hearRange == 0.0F);
}

TEST_CASE("HuInCombat answers for the attacker with a target and for the human it holds a slot on", "[ai][scripted]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    Brain& bystander = level.add({50.0F, 40.0F, 0.0F});
    CHECK(level.call("HuInCombat", {Value(2.0)}).isNil());
    CHECK(level.call("HuInCombat", {Value(1.0)}).isNil());
    thug.setTarget(&level.scene.player());
    CHECK(level.call("HuInCombat", {Value(2.0)}).number() == 1.0);
    CHECK(level.call("HuInCombat", {Value(1.0)}).number() == 1.0);
    CHECK(level.call("HuInCombat", {Value(bystander.handle())}).isNil());
    thug.setTarget(nullptr);
    CHECK(level.call("HuInCombat", {Value(2.0)}).isNil());
    CHECK(level.call("HuInCombat", {Value(1.0)}).isNil());
    CHECK(level.call("HuInCombat", {Value(99.0)}).isNil());
}

TEST_CASE("HuIsHidden answers while the human is hidden in a shadow", "[ai][scripted]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    CHECK(level.call("HuIsHidden", {Value(2.0)}).isNil());
    thug.human().enterHiding();
    CHECK(level.call("HuIsHidden", {Value(2.0)}).number() == 1.0);
    CHECK(level.call("HuIsHidden", {Value(99.0)}).isNil());
}

TEST_CASE("HuIsInScene is nil with no scenes; IsAHuman answers for live humans and objects", "[ai][scripted]") {
    Level level;
    level.add({44.0F, 40.0F, 0.0F});
    CHECK(level.call("HuIsInScene", {Value(2.0)}).isNil());
    CHECK(level.call("IsAHuman", {Value(1.0)}).number() == 1.0);
    CHECK(level.call("IsAHuman", {Value(2.0)}).number() == 1.0);
    CHECK(level.call("IsAHuman", {Value(0.0)}).isNil());
    CHECK(level.call("IsAHuman", {Value(77.0)}).isNil());
    // A live dynamic object's handle answers too, and stops once it is removed for good.
    coney::world_objects::SpawnRecord record;
    record.handle = 77.0;
    record.typeName = "crate";
    coney::world_objects::SpawnRecord* added = level.records.add(record);
    REQUIRE(added != nullptr);
    CHECK(level.call("IsAHuman", {Value(77.0)}).number() == 1.0);
    added->removed = true;
    CHECK(level.call("IsAHuman", {Value(77.0)}).isNil());
}
