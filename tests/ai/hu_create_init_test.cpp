// SPDX-License-Identifier: GPL-3.0-or-later
// What `Human_Init` (`0x00218008`) gives a human the scripts' `HuCreate` makes, beyond its model and its money
// (docs/research/human-init.md): the brain's ped type by category, the power class's switches in the human
// flags, the class's weapon in the hand and the class's own size. Each test makes the human by the scripts' `HuCreate`
// over the scripted brains, as the play mode's cast does, on configuration calls written here: no disc.
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using coney::script::Value;
namespace flag = coney::human::flag;

namespace {

// A front-end host that ignores every request: making humans asks nothing of it.
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

// A `CfgChar` call: type, category (argument 3), power class (5), the women's byte (13) and the weapon (17); the
// object (15) none and the drop chance (16) -1; the other arguments 0.
std::vector<Value> cfgChar(int type, int category, int powerClass, const std::string& weapon, bool woman = false) {
    std::vector<Value> args(17, Value(0.0));
    args[0] = Value(static_cast<double>(type));
    args[2] = Value(static_cast<double>(category));
    args[4] = Value(static_cast<double>(powerClass));
    args[12] = Value(woman ? 1.0 : 0.0);
    args[14] = Value(std::string("none"));
    args[15] = Value(-1.0);
    args[16] = Value(weapon);
    return args;
}

// A `CfgCharClassAttribs` call: a category whose humans start with exactly `dollars`, never an object or a bonus.
std::vector<Value> cfgClass(int category, int dollars) {
    return {Value(static_cast<double>(category)),
            Value(static_cast<double>(dollars)),
            Value(static_cast<double>(dollars)),
            Value(0.0),
            Value(100.0),
            Value(-1.0),
            Value(1.0)};
}

// A `CfgPowerClass` call of 31 arguments for `id`, every one 0 but the six switches (arguments 23-28).
std::vector<Value> cfgPowerClass(int id, const std::array<double, 6>& switches) {
    std::vector<Value> args(31, Value(0.0));
    args[0] = Value(static_cast<double>(id));
    for (std::size_t i = 0; i < switches.size(); ++i) {
        args[22 + i] = Value(switches.at(i));
    }
    return args;
}

// A level that has loaded: a human the scripts make gets a brain of the synthetic scene at once, as the play mode's
// cast gives it one.
struct Level {
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

    Level() {
        flags.createPool(4);
        scripted = std::make_unique<coney::ai::ScriptedBrains>(scene.brains, flags);
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
        scripted->release([this](const coney::HumanCreation& human) -> coney::ai::Brain* {
            const std::array<float, 3> at = human.position.value_or(std::array<float, 3>{});
            return &scene.add({at[0], at[1], at[2]}, human.headingDegrees);
        });
    }

    // Records a configuration call as the level's boot scripts do.
    void configure(std::string_view binding, const std::vector<Value>& args) { recorded.add(binding, args); }

    // `HuCreate("someone", type, {0, 2, 4}, 0, "", player, -1)`: the new human's brain.
    coney::ai::Brain& create(int type, int player = 0) {
        auto position = std::make_shared<coney::script::Table>();
        for (std::size_t i = 0; i < 3; ++i) {
            REQUIRE(position->set(Value(static_cast<double>(i + 1)), Value(static_cast<double>(i) * 2.0)).has_value());
        }
        const std::array<Value, 7> args{Value(std::string("someone")),
                                        Value(static_cast<double>(type)),
                                        Value(std::move(position)),
                                        Value(0.0),
                                        Value(std::string()),
                                        Value(static_cast<double>(player)),
                                        Value(-1.0)};
        coney::script::LuaVm& vm = scripts->vm();
        const auto made = vm.call(vm.global("HuCreate"), args);
        REQUIRE(made.has_value());
        REQUIRE(!made->empty());
        const double handle = made->front().number().value_or(0.0);
        const auto found = scripted->bound().find(handle);
        REQUIRE(found != scripted->bound().end());
        return *found->second;
    }
};

} // namespace

TEST_CASE("HuCreate's human of category 5 has ped type 3, of category 6 with $21 or more ped type 2",
          "[ai][hu_create]") {
    // Expected: human-init.md#ped-type, confirmed (code)
    Level level;
    level.configure("CfgChar", cfgChar(300, 5, 2, "none"));
    level.configure("CfgChar", cfgChar(301, 6, 2, "none"));
    level.configure("CfgChar", cfgChar(302, 9, 2, "none"));
    level.configure("CfgCharClassAttribs", cfgClass(5, 5));
    level.configure("CfgCharClassAttribs", cfgClass(6, 30));
    level.configure("CfgCharClassAttribs", cfgClass(9, 5));
    CHECK(level.create(300).senses().pedType == 3);
    CHECK(level.create(301).senses().pedType == 2);
    // Category 9 leaves the brain's own.
    CHECK(level.create(302).senses().pedType == 0);
}

TEST_CASE("HuCreate's ped type follows the class's record, not the given type's", "[ai][hu_create]") {
    // Expected: human-init.md#ped-type, confirmed (code): Human_Init reads the category, the women's byte, the object
    // and the drop chance from the class's record (`+0xcc`, after the remap), so type 2 rolls as class 1.
    Level level;
    level.configure("CfgChar", cfgChar(1, 5, 2, "none"));
    level.configure("CfgChar", cfgChar(2, 9, 2, "none"));
    level.configure("CfgCharClassAttribs", cfgClass(5, 5));
    level.configure("CfgCharClassAttribs", cfgClass(9, 5));
    CHECK(level.create(2).senses().pedType == 3);
}

TEST_CASE("HuCreate's human takes his power class's switches into his flags", "[ai][hu_create]") {
    // Expected: human-init.md#class-switches, confirmed (code)
    Level level;
    level.configure("CfgChar", cfgChar(300, 9, 12, "none"));
    level.configure("CfgPowerClass", cfgPowerClass(12, {1, 1, 0, 1, -1, 1}));
    const std::uint64_t flags = level.create(300).human().flags();
    CHECK((flags & flag::kUngroundable) != 0);
    CHECK((flags & flag::kHardToStun) != 0);
    CHECK((flags & flag::kNoEscape) == 0);
    CHECK((flags & flag::kBlockFromReact) != 0);
    // Argument 27 (+0x3f) at -1 is the reduced reaction; 28 (+0x3e) at 1 the increased one.
    CHECK((flags & flag::kReducedReact) != 0);
    CHECK((flags & flag::kComboStrength) == 0);
    CHECK((flags & flag::kIncreasedReact) != 0);
    CHECK((flags & flag::kIncreasedReactOff) == 0);
}

TEST_CASE("HuCreate's human holds his class's weapon from the start, unless it is none", "[ai][hu_create]") {
    // Expected: human-init.md#weapon, confirmed (code)
    Level level;
    level.configure("CfgChar", cfgChar(300, 9, 2, "dyn_baton"));
    level.configure("CfgChar", cfgChar(301, 9, 2, "none"));
    const coney::ai::Brain& cop = level.create(300);
    CHECK(cop.human().script().heldObjectName == "dyn_baton");
    // The baton is the next object the scripts' handles give, after the human's own.
    CHECK(cop.human().script().heldObject == cop.handle() + 1.0);
    const coney::ai::Brain& plain = level.create(301);
    CHECK(plain.human().script().heldObjectName.empty());
    CHECK(plain.handle() == cop.handle() + 2.0);
}

TEST_CASE("HuCreate's human of a sized class takes its size; any other is made at 1", "[ai][hu_create]") {
    // Expected: human-init.md#size, confirmed (code)
    Level level;
    // Type 35 is a variant of class 33, which is 1.03; type 32 of class 30, 0.97.
    CHECK(level.create(35).human().scale() == 1.03F);
    CHECK(level.create(32, 1).human().scale() == 0.97F);
    CHECK(level.create(417).human().scale() == 1.0F);
}

TEST_CASE("HuCreate's random size counts any woman byte but 0, and any hat name but none, an empty one too",
          "[ai][hu_create]") {
    // Expected: characters.md#body-scale, confirmed (code) (the woman byte at +0x3b8 is tested for not 0, the hat
    // name compared with "none" at 0x0021900c)
    Level level;
    // Class 300 has the women's byte 2; class 301 an empty hat name; class 302 the hat none. Category 9, brain 0.
    std::vector<Value> woman = cfgChar(300, 9, 2, "none");
    woman[12] = Value(2.0);
    woman[10] = Value(std::string("none"));
    std::vector<Value> emptyHat = cfgChar(301, 9, 2, "none");
    emptyHat[10] = Value(std::string());
    std::vector<Value> noHat = cfgChar(302, 9, 2, "none");
    noHat[10] = Value(std::string("none"));
    level.configure("CfgChar", woman);
    level.configure("CfgChar", emptyHat);
    level.configure("CfgChar", noHat);
    std::set<float> womanSizes;
    std::set<float> emptyHatSizes;
    std::set<float> noHatSizes;
    for (int i = 0; i < 12; ++i) {
        womanSizes.insert(level.create(300).human().scale());
        emptyHatSizes.insert(level.create(301).human().scale());
        noHatSizes.insert(level.create(302).human().scale());
    }
    // A woman's sizes are 1.0 to 0.97 in steps of 0.01; 0.99 and 0.97 are a woman's alone.
    for (const float size : womanSizes) {
        CHECK((size == 1.0F || size == 0.99F || size == 0.98F || size == 0.97F));
    }
    CHECK((womanSizes.contains(0.99F) || womanSizes.contains(0.97F)));
    // A hat never takes 0.98 or 1.02 (draws 31-54 give it 1.01); without one, those draws give 0.98 here.
    CHECK_FALSE(emptyHatSizes.contains(0.98F));
    CHECK_FALSE(emptyHatSizes.contains(1.02F));
    CHECK(noHatSizes.contains(0.98F));
}

TEST_CASE("HuCreate's class size is the size the human is made at, which a reset goes back to", "[ai][hu_create]") {
    // Expected: human-init.md#size, confirmed (code) (Human_Init's size and HuSetScale's both store +0x65c; the made
    // size is the one before any script's HuSetScale)
    Level level;
    const coney::human::Human& sized = level.create(35).human();
    CHECK(sized.configuredScale() == 1.03F);
    CHECK(sized.collisionScale() == 1.03F);
    CHECK(level.create(32, 1).human().configuredScale() == 0.97F);
    CHECK(level.create(417).human().configuredScale() == 1.0F);
}
