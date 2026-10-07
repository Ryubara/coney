// SPDX-License-Identifier: GPL-3.0-or-later
// The character, AI and gang bindings of the first mission from Lua to the level's humans (scripting/human_bindings.h,
// ai/scripted_humans.h): each binding called by name in a script state whose AI host is the scripted brains over a
// synthetic scene, then the humans, brains, gangs and the game state's rules checked. Handles: 1 the player, 2 and up
// the AI humans the tests add.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/formations.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_humans.h"
#include "ai/scripted_story.h"
#include "combat/combat_tuning.h"
#include "core/error.h"
#include "gui/global_strings.h"
#include "human/human_flags.h"
#include "scripting/binding_args.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/ai_fixtures.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

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
    std::unique_ptr<coney::ai::ScriptedBrains> scripted;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::BindingContext context;
    std::unique_ptr<ScriptSystem> scripts;
    std::vector<std::vector<double>> rageCalls; // the rage handler's arguments

    Level() {
        flags.createPool(4);
        scripted = std::make_unique<coney::ai::ScriptedBrains>(scene.brains, flags);
        scripted->bind(1.0, scene.player());
        scripted->setPlayer(&scene.player());
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.ai = scripted.get();
        scripts = std::make_unique<ScriptSystem>(
            [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
            },
            [this](ScriptSystem& system, LuaVm& vm) {
                coney::script::installBindings(system, vm, context);
                vm.registerFunction("RageFull", [this](std::span<const Value> args) {
                    rageCalls.emplace_back();
                    for (const Value& arg : args) {
                        rageCalls.back().push_back(arg.number().value_or(-1.0));
                    }
                    return coney::script::binding::none();
                });
            },
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

// Restores the combat tuning a test changes (the demi-god floor, the rage hold).
class TuningScope {
  public:
    TuningScope() = default;
    TuningScope(const TuningScope&) = delete;
    TuningScope& operator=(const TuningScope&) = delete;
    TuningScope(TuningScope&&) = delete;
    TuningScope& operator=(TuningScope&&) = delete;
    ~TuningScope() { coney::combat::combatTuning() = coney::combat::CombatTuning{}; }
};

} // namespace

TEST_CASE("the flag setters set and clear their bits; HuSetPreventRage writes its bit inverted", "[ai][scripted]") {
    const TuningScope scope;
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    const coney::human::Human& human = thug.human();
    level.call("HuSetUnstunnable", {Value(2.0), Value(1.0)});
    level.call("HuSetNoTarget", {Value(2.0), Value(1.0)});
    level.call("HuSetTireless", {Value(2.0), Value(1.0)});
    CHECK(human.hasFlag(flag::kUnstunnable));
    CHECK(human.hasFlag(flag::kNoTarget));
    CHECK(human.hasFlag(flag::kTireless));
    level.call("HuSetNoTarget", {Value(2.0), Value()});
    CHECK_FALSE(human.hasFlag(flag::kNoTarget));
    level.call("HuSetPreventRage", {Value(1.0), Value(1.0)});
    CHECK_FALSE(level.scene.player().human().hasFlag(flag::kRageAllowed));
    level.call("HuSetPreventRage", {Value(1.0), Value()});
    CHECK(level.scene.player().human().hasFlag(flag::kRageAllowed));
    // God mode takes only exactly 1; demi-god mode keeps its fraction in the one global.
    level.call("HuSetGodMode", {Value(2.0), Value(2.0)});
    CHECK_FALSE(human.hasFlag(flag::kGod));
    level.call("HuSetGodMode", {Value(2.0), Value(1.0)});
    CHECK(human.hasFlag(flag::kGod));
    level.call("HuSetDemiGodMode", {Value(2.0), Value(1.0), Value(0.4)});
    CHECK(human.hasFlag(flag::kDemiGod));
    CHECK(coney::combat::combatTuning().healthFloor == 0.4F);
}

TEST_CASE("the getters answer for a human and their defaults for a handle that names none", "[ai][scripted]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    CHECK(level.call("HuIsAlive", {Value(2.0)}).number() == 1.0);
    CHECK(level.call("HuIsAPlayer", {Value(1.0)}).number() == 1.0);
    CHECK(level.call("HuIsAPlayer", {Value(2.0)}).isNil());
    CHECK(level.call("HuGetHealthPercent", {Value(2.0)}).number() == 100.0);
    CHECK(level.call("HuGetGangType", {Value(2.0)}).number() == 65535.0);
    const int gang = level.scene.brains.gangs().create(19, "Thugs");
    level.scene.brains.gangs().addMember(gang, thug);
    CHECK(level.call("HuGetGangType", {Value(2.0)}).number() == 19.0);
    level.call("HuSetArrested", {Value(2.0), Value(1.0)});
    CHECK(level.call("HuIsArrested", {Value(2.0)}).number() == 1.0);
    CHECK(level.call("HuIsAlive", {Value(2.0)}).isNil());
    level.call("HuSetHealthPercent", {Value(2.0), Value(25.0)});
    CHECK(level.call("HuGetHealthPercent", {Value(2.0)}).number() == 25.0);
    // No such human.
    CHECK(level.call("HuIsAlive", {Value(99.0)}).isNil());
    CHECK(level.call("HuGetHealthPercent", {Value(99.0)}).number() == 0.0);
    CHECK(level.call("HuGetGangType", {Value(99.0)}).number() == 65535.0);
    CHECK(level.call("HuGetHeldObject", {Value(99.0)}).number() == 0.0);
}

TEST_CASE("a call on a human the level has not made yet waits for it", "[ai][scripted]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    level.scripted->hold();
    level.call("HuSetUngrabbable", {Value(2.0), Value(1.0)});
    level.call("HuSetMoney", {Value(2.0), Value(1500.0)});
    const double item = level.call("HuPlaceItemInHand", {Value(2.0), Value("dyn_bat")}).number().value_or(0.0);
    CHECK(item != 0.0);
    CHECK_FALSE(thug.human().hasFlag(flag::kUngrabbable));
    CHECK(level.scripted->held() == 3);
    level.scripted->release({});
    CHECK(thug.human().hasFlag(flag::kUngrabbable));
    CHECK(thug.human().script().money == 999);
    CHECK(level.call("HuGetHeldObject", {Value(2.0)}).number() == item);
    // A full hand takes nothing more until the weapon is dropped.
    CHECK(level.call("HuPlaceItemInHand", {Value(2.0), Value("dyn_pipe_a")}).number() == 0.0);
    level.call("HuDropWeapon", {Value(2.0)});
    CHECK(level.call("HuGetHeldObject", {Value(2.0)}).number() == 0.0);
}

TEST_CASE("EnableCommand masks a player's commands; an AI human has no pad", "[ai][scripted]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    level.call("EnableCommand", {Value(1.0), Value(38.0), Value(0.0)});
    level.call("EnableCommand", {Value(1.0), Value(37.0), Value(0.0)});
    level.call("EnableCommand", {Value(1.0), Value(58.0), Value(0.0)}); // out of range: ignored
    const std::uint64_t off = level.scene.player().human().script().disabledCommands;
    CHECK(off == ((std::uint64_t{1} << 37) | (std::uint64_t{1} << 38)));
    level.call("EnableCommand", {Value(1.0), Value(37.0), Value(1.0)});
    CHECK(level.scene.player().human().script().disabledCommands == (std::uint64_t{1} << 38));
    level.call("EnableCommands", {Value(1.0), Value(1.0)});
    CHECK(level.scene.player().human().script().disabledCommands == 0);
    level.call("EnableCommand", {Value(2.0), Value(37.0), Value(0.0)});
    CHECK(thug.human().script().disabledCommands == 0);
    level.call("HuLockPad", {Value(1.0), Value(1.0)});
    CHECK(level.scene.player().human().script().padLocked);
}

TEST_CASE("an invincible gang's members, present and later, are gods; an untargetable gang's are skipped",
          "[ai][scripted]") {
    Level level;
    Brain& first = level.add({44.0F, 40.0F, 0.0F});
    Brain& second = level.add({46.0F, 40.0F, 0.0F});
    coney::ai::Gangs& gangs = level.scene.brains.gangs();
    const int gang = gangs.create(0, "Warriors2");
    gangs.addMember(gang, first);
    level.call("GangInvincible", {Value(static_cast<double>(gang)), Value(1.0)});
    CHECK(first.human().hasFlag(flag::kGod));
    gangs.addMember(gang, second);
    CHECK(second.human().hasFlag(flag::kGod));
    level.call("GangInvincible", {Value(static_cast<double>(gang)), Value()});
    CHECK_FALSE(first.human().hasFlag(flag::kGod));
    level.call("GangSetTargetable", {Value(static_cast<double>(gang)), Value()});
    CHECK_FALSE(first.human().targetable());
    level.call("GangSetTargetable", {Value(static_cast<double>(gang))});
    CHECK(first.human().targetable());
}

TEST_CASE("GoalBackoff walks away from the other human; BrClearBackoff pops it", "[ai][scripted]") {
    Level level;
    Brain& thug = level.add({41.0F, 40.0F, 0.0F});
    level.call("GoalBackoff", {Value(2.0), Value(1.0), Value(4.0)});
    REQUIRE(thug.topGoal() != nullptr);
    CHECK(thug.topGoal()->type() == coney::ai::GoalType::Backoff);
    level.scene.run(60);
    CHECK(thug.distanceTo(level.scene.player()) > 2.0F);
    level.call("BrClearBackoff", {Value(2.0)});
    CHECK(thug.goalCount() == 0);
}

TEST_CASE("GoalMoveToUseFlag reserves the flag until its goal ends; GoalBumLogic keeps a bum in place",
          "[ai][scripted]") {
    Level level;
    Brain& extra = level.add({41.0F, 41.0F, 0.0F});
    const double spot = level.flags.add(100.0, "Chair", {44.0F, 41.0F, 0.0F}, 90.0F).handle;
    level.call("GoalMoveToUseFlag",
               {Value(2.0), Value(spot), Value(2.0), Value(0.0), Value(0.5), Value(0.5), Value(1.0)});
    REQUIRE(extra.topGoal() != nullptr);
    CHECK(extra.topGoal()->type() == coney::ai::GoalType::MoveToUseFlag);
    CHECK(level.scripted->humanHost().reservation(spot) == 2.0);
    extra.flush();
    CHECK(level.scripted->humanHost().reservation(spot) == 0.0);

    level.call("GoalBumLogic", {Value(2.0), Value(2.0), Value(), Value(175.0)});
    REQUIRE(extra.topGoal() != nullptr);
    CHECK(extra.topGoal()->type() == coney::ai::GoalType::BumLogic);
}

TEST_CASE("the configuration bindings set the game state's rules, which the level takes", "[ai][scripted]") {
    const TuningScope scope;
    Level level;
    level.call("CfgPlayerMugging", {Value()});
    level.call("CfgSetEnemySpotting", {});
    level.call("CfgSetWarriorSpotting", {Value(1.0)});
    level.call("CfgSetGlobalTimeToLive", {Value(1000.0)});
    const coney::CharacterRules& rules = level.state.characters;
    CHECK_FALSE(rules.playerMugging);
    CHECK(rules.enemySpotting);
    CHECK_FALSE(rules.warriorSpotting); // the original always clears it
    CHECK(rules.timeToLiveMs == 1000);

    level.call("CfgRageHandlers", {Value("SetRageMode"), Value(), Value("RageFull"), Value(20000.0), Value(4000.0)});
    CHECK(rules.rage.onEnter == "SetRageMode");
    CHECK(rules.rage.onExit.empty());
    CHECK(coney::combat::combatTuning().rageHoldMs == 4000);

    // The default follow slots reach the formations, those made later too.
    auto slots = std::make_shared<coney::script::Table>();
    for (int i = 1; i <= 20; ++i) {
        REQUIRE(slots->set(Value(static_cast<double>(i)), Value(static_cast<double>(i))).has_value());
    }
    level.call("CfgSetDefaultFollowSlotSet", {Value(1.0), Value(slots)});
    // A local: clang-tidy cannot follow a check through the array's subscript.
    const auto& slotSet = rules.followSlots[1];
    REQUIRE(slotSet.has_value());
    if (!slotSet) {
        return;
    }
    CHECK((*slotSet)[8].first == 17.0F);
    const coney::ai::Formation* formation = level.scene.brains.formations().of(level.scene.player(), true);
    REQUIRE(formation != nullptr);
    CHECK(formation->slot(1, 0).offset[0] == 16); // 1 m in sixteenths
    CHECK(formation->slot(1, 0).offset[1] == 32);

    level.call("SetInterrogateParam",
               {Value(160.0), Value(75.0), Value(255.0), Value(5000.0), Value(2500.0), Value(20000.0), Value(40.0),
                Value(60.0), Value(20000.0), Value(0.0), Value(4.0)});
    CHECK(rules.interrogate[1].active());
    CHECK(rules.interrogate[1].values[0] == 160);
    CHECK_FALSE(rules.interrogate[0].active());
}

TEST_CASE("a script's follow slots survive later configuration calls; only the defaults' own call rewrites them",
          "[ai][scripted]") {
    const TuningScope scope;
    Level level;
    // A table of numbers 1..n.
    const auto numbers = [](std::initializer_list<double> values) {
        auto table = std::make_shared<coney::script::Table>();
        double key = 1.0;
        for (const double value : values) {
            REQUIRE(table->set(Value(key), Value(value)).has_value());
            key += 1.0;
        }
        return Value(table);
    };
    // The defaults first (a level's configuration), then lesson 7's four slots 1 m around the player, ahead, right,
    // behind and left (docs/research/ai.md#level99-snaps).
    level.call("CfgSetDefaultFollowSlotSet",
               {Value(0.0), numbers({-1.25, -1.0, 1.25, -1.0, -2.75, -1.0, 2.75, -1.0, 0.0, -2.0,
                                     0.0,   -3.0, 0.0,  -4.0, 0.0,   -5.0, 0.0,  -6.0, 0.0, 0.0})});
    level.call("BrSetFollowSlotSet", {Value(1.0), Value(0.0)});
    level.call("BrSetNumFollowSlots", {Value(1.0), Value(4.0)});
    const std::array<std::pair<double, double>, 4> around{{{0.0, 1.0}, {1.0, 0.0}, {0.0, -1.0}, {-1.0, 0.0}}};
    for (std::size_t slot = 0; slot < around.size(); ++slot) {
        level.call("BrSetFollowSlot", {Value(1.0), Value(static_cast<double>(slot)),
                                       numbers({around.at(slot).first, around.at(slot).second}), Value(0.0)});
    }
    const coney::ai::Formation* formation = level.scene.brains.formations().of(level.scene.player(), false);
    REQUIRE(formation != nullptr);
    // In sixteenths of a metre.
    const auto offsetOf = [&formation](int slot) {
        return std::pair<int, int>{formation->slot(0, slot).offset[0], formation->slot(0, slot).offset[1]};
    };
    const auto checkAround = [&] {
        CHECK(offsetOf(0) == std::pair<int, int>{0, 16});
        CHECK(offsetOf(1) == std::pair<int, int>{16, 0});
        CHECK(offsetOf(2) == std::pair<int, int>{0, -16});
        CHECK(offsetOf(3) == std::pair<int, int>{-16, 0});
    };
    checkAround();
    // EnableAllButtons reaches a rules binding (WCEnableAllCommands); it and the others leave the slots alone.
    level.call("WCEnableAllCommands", {Value(1.0)});
    level.call("CfgSetEnemySpotting", {});
    level.call("TurnWarriorCommands", {Value(1.0)});
    checkAround();
    // A formation made now still starts from the defaults.
    coney::ai::Brain& other = level.scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    const coney::ai::Formation* fresh = level.scene.brains.formations().of(other, true);
    REQUIRE(fresh != nullptr);
    CHECK(fresh->slot(0, 0).offset[0] == -20);
    CHECK(fresh->slot(0, 0).offset[1] == -16);
    // CfgSetDefaultFollowSlotSet itself writes its set into every formation, the player's too.
    level.call("CfgSetDefaultFollowSlotSet", {Value(0.0), numbers({0.5, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                                   0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0})});
    CHECK(offsetOf(0) == std::pair<int, int>{8, 8});
}

TEST_CASE("the rage handler runs when a human's meter fills, with its handle", "[ai][scripted]") {
    const TuningScope scope;
    Level level;
    level.call("CfgRageHandlers", {Value(), Value(), Value("RageFull"), Value(20000.0), Value(5000.0)});
    level.scripted->humanHost().runRageHandlers();
    CHECK(level.rageCalls.empty());
    level.call("HuSetFullRage", {Value(1.0)});
    level.scripted->humanHost().runRageHandlers();
    level.scripted->humanHost().runRageHandlers();
    REQUIRE(level.rageCalls.size() == 1);
    CHECK(level.rageCalls[0] == std::vector<double>{1.0});
}

TEST_CASE("SetDynamicAnimation lists the clips HuUseAnim may use", "[ai][scripted]") {
    Level level;
    Brain& extra = level.add({44.0F, 40.0F, 0.0F});
    CHECK(level.call("HuUseAnim", {Value(2.0), Value(0.0), Value("fidget_crossarms.anm")}).isNil());
    level.call("SetDynamicAnimation", {Value("fidget_crossarms.anm")});
    CHECK(level.call("HuUseAnim", {Value(2.0), Value(0.0), Value("fidget_crossarms.anm")}).number() == 1.0);
    CHECK(extra.human().script().animOverride(0x184) == "fidget_crossarms.anm");
    CHECK_FALSE(extra.human().script().pushable);
    CHECK(level.call("HuUseAnim", {Value(2.0), Value(4.0), Value("fidget_crossarms.anm")}).isNil());
    level.call("SetDynamicAnimation", {Value("fidget_crossarms.anm"), Value(1.0)});
    CHECK(level.state.characters.dynamicAnimations.empty());
}

TEST_CASE("HuUseAnyAnim puts a requested clip in the slot for any anim id, and frees it", "[ai][scripted]") {
    Level level;
    Brain& extra = level.add({44.0F, 40.0F, 0.0F});
    const coney::human::ScriptState& script = extra.human().script();
    // Not requested: refused. Requested: put in for anim 666 (ANIM_SPECIAL_OPEN_DOOR), the pushable flag untouched.
    CHECK(level.call("HuUseAnyAnim", {Value(2.0), Value(666.0), Value("open_door.anm")}).isNil());
    level.call("SetDynamicAnimation", {Value("open_door.anm")});
    level.call("SetDynamicAnimation", {Value("kick_door.anm")});
    CHECK(level.call("HuUseAnyAnim", {Value(2.0), Value(666.0), Value("open_door.anm")}).number() == 1.0);
    CHECK(script.animOverride(666) == "open_door.anm");
    CHECK(script.pushable);
    // The same id reuses its slot.
    CHECK(level.call("HuUseAnyAnim", {Value(2.0), Value(666.0), Value("kick_door.anm")}).number() == 1.0);
    CHECK(script.animOverride(666) == "kick_door.anm");
    CHECK(std::ranges::count_if(script.animOverrides, [](const auto& slot) { return !slot.clip.empty(); }) == 1);
    // Freed by an empty name; freeing again finds nothing.
    CHECK(level.call("HuUseAnyAnim", {Value(2.0), Value(666.0), Value()}).number() == 1.0);
    CHECK(script.animOverride(666).empty());
    CHECK(level.call("HuUseAnyAnim", {Value(2.0), Value(666.0), Value()}).isNil());
    // A human that is not there: refused.
    CHECK(level.call("HuUseAnyAnim", {Value(99.0), Value(666.0), Value("open_door.anm")}).isNil());
}

TEST_CASE("A human's seven dynamic animation slots fill in order and refuse an eighth id", "[ai][scripted]") {
    coney::human::ScriptState script;
    for (std::uint32_t id = 600; id < 607; ++id) {
        CHECK(script.setAnimOverride(id, "clip.anm"));
    }
    CHECK_FALSE(script.setAnimOverride(607, "clip.anm"));
    CHECK(script.animOverride(607).empty());
    // A freed slot is the first one taken next.
    CHECK(script.setAnimOverride(602, {}));
    CHECK(script.setAnimOverride(607, "other.anm"));
    CHECK(script.animOverrides[2].animId == 607);
    CHECK(script.animOverride(607) == "other.anm");
}

TEST_CASE("LoadBumAnims requests the bum animations as one set and releases them", "[ai][scripted]") {
    Level level;
    level.call("SetDynamicAnimation", {Value("point_left.anm")});
    level.call("LoadBumAnims", {Value(1.0)});
    const std::vector<std::string>& list = level.state.characters.dynamicAnimations;
    // Thirteen requests, one of them a name already listed: twelve files beside the script's own.
    CHECK(list.size() == 13);
    CHECK(list[1] == "puke_fidget.anm");
    CHECK(list.back() == "bum_beg_hit.anm");
    level.call("LoadBumAnims", {Value()});
    CHECK(list == std::vector<std::string>{"point_left.anm"});
}

TEST_CASE("WCIssueCommand reaches a player's crew only while the command is enabled", "[ai][scripted]") {
    Level level;
    level.call("WCIssueCommand", {Value(1.0), Value(3.0), Value(1.0)});
    CHECK(level.state.characters.lastWarriorCommand[0] == 3);
    level.call("WCEnableAllCommands", {Value()});
    level.call("WCIssueCommand", {Value(1.0), Value(5.0), Value(1.0)});
    CHECK(level.state.characters.lastWarriorCommand[0] == 3);
    CHECK(level.scripted->storyHost().warriorCommand() == 3);
    CHECK(level.call("HuChangePlayerGang", {Value(4.0), Value(1.0)}).number() == 1.0);
    CHECK(level.scripted->humanHost().playerGang() == 4);
}

TEST_CASE("HuChangePlayerGang hands player 1 to the new gang's lowest priority; GangDelete deletes its members",
          "[ai][scripted]") {
    Level level;
    Brain& player = level.scene.player();
    // The play mode's parts: a created human is an AI human of the scene; the hand-over and the deletions are noted.
    level.scripted->release([&level](const coney::HumanCreation& human) -> Brain* {
        return &level.scene.add({human.position ? (*human.position)[0] : 44.0F, 40.0F, 0.0F}, 0.0F);
    });
    std::vector<double> handedTo;
    level.scripted->setHandOver([&handedTo](const Brain& to) { handedTo.push_back(to.handle()); });
    std::vector<double> removed;
    level.scripted->setRemover([&removed](Brain& brain) { removed.push_back(brain.handle()); });
    // level99's checkpoint 2 set-up: the old gang holds player 1 and his team-mate; the new one Ash (2) and Rembrandt
    // (1), Ash made first.
    const auto create = [&level](std::string_view name, double priority, double gang) {
        const std::array<double, 3> at{50.0, 40.0, 0.0};
        auto position = std::make_shared<coney::script::Table>();
        for (std::size_t i = 0; i < at.size(); ++i) {
            REQUIRE(position->set(Value(static_cast<double>(i + 1)), Value(at.at(i))).has_value());
        }
        return level
            .call("HuCreate", {Value(std::string(name)), Value(30.0), Value(position), Value(0.0), Value("warr_sw"),
                               Value(priority), Value(gang)})
            .number()
            .value_or(0.0);
    };
    const double oldGang = level.call("GangCreate", {Value(0.0), Value("Warriors2")}).number().value_or(-1.0);
    level.call("GangAddMember", {Value(oldGang), Value(1.0)});
    const double oldAsh = create("Ash", 2.0, oldGang);
    const double newGang = level.call("GangCreate", {Value(0.0), Value("Warriors")}).number().value_or(-1.0);
    const double ash = create("Ash", 2.0, newGang);
    const double rembrandt = create("Rembrandt", 1.0, newGang);
    REQUIRE(oldAsh != 0.0);
    REQUIRE(rembrandt != 0.0);
    // The second player 1 stays an AI human while player 1 is listed.
    CHECK(level.scripted->brain(rembrandt) != &player);

    level.call("HuChangePlayerGang", {Value(newGang)});
    CHECK(handedTo == std::vector<double>{rembrandt});
    CHECK(level.scripted->brain(rembrandt) == &player);
    CHECK(level.scripted->brain(1.0) == nullptr);
    CHECK(player.handle() == rembrandt);
    REQUIRE(player.gang() != nullptr);
    CHECK(player.gang()->id() == static_cast<int>(newGang));
    CHECK(level.scripted->brain(ash) != nullptr);
    // Already in the gang: nothing more.
    level.call("HuChangePlayerGang", {Value(newGang)});
    CHECK(handedTo.size() == 1);

    // The old gang goes with the old team-mate in it; the player, in the new gang, stays.
    level.call("GangDelete", {Value(oldGang)});
    CHECK(removed == std::vector<double>{oldAsh});
    CHECK(level.scripted->brain(oldAsh) == nullptr);
    CHECK(level.scripted->brain(rembrandt) == &player);
    // A gang deleted with player 1 in it keeps him: Coney has no other human to give him.
    level.call("GangDelete", {Value(newGang)});
    CHECK(removed == std::vector<double>{oldAsh, ash});
    CHECK(level.scripted->brain(rembrandt) == &player);
    CHECK(player.gang() == nullptr);
}
