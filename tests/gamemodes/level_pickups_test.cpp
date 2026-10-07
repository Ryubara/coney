// SPDX-License-Identifier: GPL-3.0-or-later
// A level's loose objects as a player uses them with triangle (docs/research/combat.md#breakables,
// docs/research/combat.md#bat): an object's message 0 and its prompt, the search over the spawn records, the take,
// which adds a TYPE_SPECIAL's loot and money through the inventory callbacks or puts a weapon in hand, and the drop.
#include "gamemodes/level_pickups.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/binding_args.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/message_handlers.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "warriors/inventory.h"

using coney::anim::Vec3;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;
namespace item = coney::item;
namespace wo = coney::world_objects;

namespace {

// A host that ignores every request: the pick-up asks nothing of it.
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

// The bindings over a game state, a watch type (loot worth 7), a bat type and a crate type, a native `Item` recording
// the inventory callback's items and `Touch` and `Keep` message 0 handlers (the second takes the press).
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans};
    ScriptSystem scripts;
    wo::SpawnRecords records;
    wo::ObjectTypes types;
    coney::script::MessageHandlers messages;
    std::vector<double> items;
    std::vector<double> touched; // the objects `Touch` and `Keep` heard

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        scripts.create();
        scripts.vm().registerFunction("Item", [this](std::span<const Value> args) -> coney::script::binding::Results {
            items.push_back(args[0].number().value_or(-1.0));
            return std::vector<Value>{};
        });
        // A handler of message 0 that does not take the press, and one that does.
        scripts.vm().registerFunction("Touch", [this](std::span<const Value> args) -> coney::script::binding::Results {
            touched.push_back(args[0].number().value_or(-1.0));
            return std::vector<Value>{};
        });
        scripts.vm().registerFunction("Keep", [this](std::span<const Value> args) -> coney::script::binding::Results {
            touched.push_back(args[0].number().value_or(-1.0));
            return std::vector<Value>{Value(1.0)};
        });
        const std::vector<Value> callback{Value(std::string("Item"))};
        REQUIRE(scripts.vm().call(scripts.vm().global("CfgInventoryCallback"), callback).has_value());
        wo::ObjectType watch;
        watch.name = "dyn_watch";
        watch.className = "pickup_item";
        watch.value = 7;
        watch.objectKind = wo::kObjectKindSpecial;
        watch.pickupAnim = wo::kPickupOneHanded;
        types.add(watch);
        wo::ObjectType bat;
        bat.name = "dyn_bat_tuff";
        bat.className = "melee_weapon";
        bat.pickupAnim = 1;
        bat.objectKind = 3;
        bat.animSet = 3;
        types.add(bat);
        types.add("crate", "simple_object", 0);
    }

    // A record of `type` with `handle` at `position` in `zone`.
    wo::SpawnRecord* place(double handle, const char* type, Vec3 position, std::uint32_t zone = 0) {
        wo::SpawnRecord record;
        record.handle = handle;
        record.typeName = type;
        record.position = {position.x, position.y, position.z};
        record.zone = zone;
        return records.add(record);
    }
};

constexpr Vec3 kFeet{10.0F, 10.0F, 0.0F};
constexpr Vec3 kFacingY{0.0F, 1.0F, 0.0F};

} // namespace

TEST_CASE("the search skips hidden records, disabled zones and classes that are not picked up", "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    h.place(1, "crate", Vec3{10.0F, 10.5F, 0.0F});
    h.place(2, "dyn_watch", Vec3{10.0F, 10.6F, 1.54F}, 26);
    h.place(3, "dyn_watch", Vec3{10.0F, 10.7F, 1.54F})->hidden = true;
    CHECK_FALSE(pickups.search(kFeet, kFacingY, {}).has_value());
    h.records.setZoneEnabled(26, true);
    const std::optional<coney::PickupChoice> choice = pickups.search(kFeet, kFacingY, {});
    REQUIRE(choice.has_value());
    const coney::PickupChoice chosen = choice.value_or(coney::PickupChoice{});
    CHECK(chosen.handle == 2);
    CHECK(chosen.clip == wo::kPickupHighClip);
}

TEST_CASE("taking loot adds item 10 with the callback, then its value in money, and removes it", "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    h.place(5, "dyn_watch", Vec3{10.0F, 11.0F, 0.3F});
    CHECK(pickups.take(5, 0) == coney::TakeResult::Loot);
    CHECK(h.state.player.inventory.count(0, item::kStolenLoot) == 1);
    CHECK(h.state.player.inventory.count(0, item::kMoney) == 7);
    // The money goes in without notify: the callback heard the loot alone.
    CHECK(h.items == std::vector<double>{item::kStolenLoot});
    CHECK(h.records.find(5)->removed);
    CHECK(pickups.take(5, 0) == coney::TakeResult::Gone);
    CHECK(pickups.take(99, 0) == coney::TakeResult::Gone);
}

TEST_CASE("a bat goes into the hand, out of the search, and back when dropped", "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    h.place(7, "dyn_bat_tuff", Vec3{10.0F, 10.8F, 0.05F});
    const std::optional<coney::PickupChoice> choice = pickups.search(kFeet, kFacingY, {});
    REQUIRE(choice.has_value());
    CHECK(choice.value_or(coney::PickupChoice{}).clip == wo::kPickupGroundClip);
    CHECK(pickups.take(7, 0) == coney::TakeResult::InHand);
    CHECK(pickups.inHand(7));
    CHECK_FALSE(h.records.find(7)->removed);
    CHECK(pickups.typeOf(7) == "dyn_bat_tuff");
    CHECK(pickups.animSetOf("dyn_bat_tuff") == 3);
    CHECK(pickups.animSetOf("dyn_watch") == 0);
    CHECK_FALSE(pickups.search(kFeet, kFacingY, {}).has_value());
    pickups.drop(7, Vec3{12.0F, 12.0F, 0.0F});
    CHECK_FALSE(pickups.inHand(7));
    CHECK(h.records.find(7)->position[0] == 12.0F);
}

TEST_CASE("triangle offers message 0 first: a prompt's object, then each object in reach", "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types, &h.messages);
    h.place(7, "dyn_bat_tuff", Vec3{10.0F, 10.8F, 0.05F});
    h.place(8, "dyn_bat_tuff", Vec3{10.0F, 11.2F, 0.05F});
    // A handler that lets the press go on: the bat is picked up after both handlers heard it.
    h.messages.set(7, 0, "Touch");
    h.messages.setPrompt(7, "Touch", "Get Weapon");
    coney::TriangleOutcome outcome = pickups.triangle(100, kFeet, kFacingY, false, {});
    CHECK(outcome.result == coney::TriangleResult::PickUp);
    CHECK(outcome.choice.handle == 7);
    // From the prompt (step 4), then from the search's gathering (step 5).
    CHECK(h.touched == std::vector<double>{7, 7});
    // A handler returning a value takes the press.
    h.touched.clear();
    h.messages.set(8, 0, "Keep");
    outcome = pickups.triangle(100, kFeet, kFacingY, false, {});
    CHECK(outcome.result == coney::TriangleResult::Consumed);
    // An empty prompt unregisters the context record.
    h.messages.setPrompt(7, "Touch", "");
    CHECK(h.messages.prompts().empty());
}

TEST_CASE("with something in hand triangle takes loot, but drops it for a weapon or nothing", "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types, &h.messages);
    h.place(7, "dyn_bat_tuff", Vec3{10.0F, 9.4F, 0.05F});
    CHECK(pickups.triangle(100, kFeet, kFacingY, true, {}).result == coney::TriangleResult::Drop);
    // A watch ahead outscores the bat behind: loot is taken whatever is in hand.
    h.place(5, "dyn_watch", Vec3{10.0F, 10.5F, 0.3F});
    const coney::TriangleOutcome outcome = pickups.triangle(100, kFeet, kFacingY, true, {});
    CHECK(outcome.result == coney::TriangleResult::PickUp);
    CHECK(outcome.choice.handle == 5);
    CHECK(pickups.triangle(100, Vec3{40.0F, 40.0F, 0.0F}, kFacingY, false, {}).result ==
          coney::TriangleResult::Nothing);
}

TEST_CASE("a prompt on an object with no spawn record (a tag spot's flag) is the action object", "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types, &h.messages);
    h.messages.set(11, 0, "Keep");
    h.messages.setPrompt(11, "Keep", "Tag");
    // Without a locator the object is nowhere: no prompt, and triangle has nothing to hand it.
    CHECK_FALSE(pickups.actionObject(kFeet).has_value());
    CHECK(pickups.triangle(100, kFeet, kFacingY, false, {}).result == coney::TriangleResult::Nothing);
    // Located 1 m away on the wall at waist height: its text is the prompt and triangle gives it the press.
    pickups.setLocator([](double object) -> std::optional<Vec3> {
        return object == 11 ? std::optional<Vec3>(Vec3{11.0F, 10.0F, 1.2F}) : std::nullopt;
    });
    const std::optional<coney::ActionObject> object = pickups.actionObject(kFeet);
    REQUIRE(object.has_value());
    CHECK(object.value_or(coney::ActionObject{}).handle == 11);
    CHECK(object.value_or(coney::ActionObject{}).prompt == "Tag");
    CHECK(pickups.triangle(100, kFeet, kFacingY, false, {}).result == coney::TriangleResult::Consumed);
    CHECK(h.touched == std::vector<double>{11});
    // Out of reach: 1.2 m away in the plane, or more than 1.5 m above the waist.
    CHECK_FALSE(pickups.actionObject(Vec3{9.8F, 10.0F, 0.0F}).has_value());
    CHECK_FALSE(pickups.actionObject(Vec3{10.5F, 10.0F, -1.4F}).has_value());
    CHECK(pickups.actionObject(Vec3{10.5F, 10.0F, -1.2F}).has_value());
}
