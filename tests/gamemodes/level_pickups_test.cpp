// SPDX-License-Identifier: GPL-3.0-or-later
// A level's loose objects as a player uses them with triangle (docs/research/combat.md#breakables,
// docs/research/combat.md#bat): an object's message 0 and its prompt, the search over the spawn records, the take,
// which adds a TYPE_SPECIAL's loot and money through the inventory callbacks or puts a weapon in hand, and the drop.
#include "gamemodes/level_pickups.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/name_hash.h"
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

TEST_CASE("walking over money takes its value with the callback and removes it", "[level_pickups]") {
    // docs/research/player-state.md#walk-over: TYPE_MONEY gives item 2 x the object's value, notifying.
    Harness h;
    wo::ObjectType cash;
    cash.name = "dyn_money";
    cash.className = "powerup_item";
    cash.objectKind = wo::kObjectKindMoney;
    h.types.add(cash);
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    h.place(6, "dyn_money", Vec3{10.0F, 11.0F, 1.6F})->money = 37;
    h.place(7, "dyn_money", Vec3{10.0F, 12.5F, 0.0F})->money = 30;
    h.place(8, "dyn_watch", Vec3{10.0F, 10.5F, 0.0F});
    // Beyond the reach in plan, or above the body, nothing is touched.
    CHECK(pickups.walkOver(0, Vec3{10.0F, 9.8F, 0.0F}, false).empty());
    CHECK(pickups.walkOver(0, Vec3{10.0F, 10.5F, -2.5F}, false).empty());
    // Out of sight from both heights, it is not touched either.
    const wo::SightBlocked wall = [](Vec3 /*from*/, Vec3 /*to*/) { return true; };
    CHECK(pickups.walkOver(0, Vec3{10.0F, 10.5F, 0.0F}, false, wall).empty());
    const std::vector<coney::LevelPickups::WalkedOver> taken = pickups.walkOver(0, Vec3{10.0F, 10.5F, 0.0F}, false);
    REQUIRE(taken.size() == 1);
    CHECK(taken[0].handle == 6);
    CHECK(taken[0].item == item::kMoney);
    CHECK(taken[0].amount == 37);
    CHECK(h.state.player.inventory.count(0, item::kMoney) == 37);
    CHECK(h.items == std::vector<double>{item::kMoney});
    CHECK(h.records.find(6)->removed);
    // The loot beside it is not a power-up: it stays for triangle.
    CHECK_FALSE(h.records.find(8)->removed);
    CHECK(pickups.walkOver(0, Vec3{10.0F, 10.5F, 0.0F}, false).empty());
}

TEST_CASE("walking over a key, a flash or a spray can gives one, below its limit", "[level_pickups]") {
    // docs/research/player-state.md#walk-over: TYPE_KEY item 6, TYPE_REVIVAL item 1, TYPE_SPRAYCAN item 3 (at most 9),
    // TYPE_SPECIAL loot; each notifying.
    Harness h;
    const auto powerup = [&h](const char* name, int kind, int value = 0) {
        wo::ObjectType type;
        type.name = name;
        type.className = "powerup_item";
        type.objectKind = kind;
        type.value = value;
        h.types.add(type);
    };
    powerup("dyn_key", wo::kObjectKindKey);
    powerup("dyn_revival", wo::kObjectKindRevival);
    powerup("dyn_spraycan", wo::kObjectKindSpraycan);
    powerup("dyn_record", wo::kObjectKindSpecial, 12);
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    coney::Inventory& inventory = h.state.player.inventory;
    constexpr Vec3 kAt{10.0F, 10.5F, 0.0F};
    SECTION("each kind gives its item and goes") {
        h.place(1, "dyn_key", kAt);
        h.place(2, "dyn_revival", kAt);
        h.place(3, "dyn_spraycan", kAt);
        h.place(4, "dyn_record", kAt);
        const std::vector<coney::LevelPickups::WalkedOver> taken = pickups.walkOver(0, kFeet, false);
        CHECK(taken.size() == 4);
        CHECK(inventory.count(0, item::kHandcuffKey) == 1);
        CHECK(inventory.count(0, item::kRevive) == 1);
        CHECK(inventory.count(0, item::kSprayPaint) == 1);
        CHECK(inventory.count(0, item::kStolenLoot) == 1);
        CHECK(inventory.count(0, item::kMoney) == 12);
        // Every gift notified but the loot's money.
        CHECK(h.items == std::vector<double>{item::kHandcuffKey, item::kRevive, item::kSprayPaint, item::kStolenLoot});
        for (const double handle : {1.0, 2.0, 3.0, 4.0}) {
            CHECK(h.records.find(handle)->removed);
        }
    }
    SECTION("a full spray-paint count or a full set of flashes refuses it, and it stays") {
        inventory.set(0, item::kSprayPaint, coney::LevelPickups::kSprayPaintMost);
        inventory.set(0, item::kRevive, coney::Inventory::kRevives);
        h.place(2, "dyn_revival", kAt);
        h.place(3, "dyn_spraycan", kAt);
        CHECK(pickups.walkOver(0, kFeet, false).empty());
        CHECK_FALSE(h.records.find(2)->removed);
        CHECK_FALSE(h.records.find(3)->removed);
        inventory.set(0, item::kSprayPaint, 8);
        CHECK(pickups.walkOver(0, kFeet, false).size() == 1);
        CHECK(inventory.count(0, item::kSprayPaint) == coney::LevelPickups::kSprayPaintMost);
    }
    SECTION("at full health with CfgPowerupPickup off a flash is walked past") {
        h.place(2, "dyn_revival", kAt);
        h.state.hub.powerupPickup = false;
        CHECK(pickups.walkOver(0, kFeet, true).empty());
        CHECK(pickups.walkOver(0, kFeet, false).size() == 1);
    }
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

TEST_CASE("a class's own message 0 lifts the object, or keeps the search off it", "[level_pickups]") {
    // docs/research/script-types.md#dyn-cashreg: a register asks to be lifted (0x14), and once broken refuses.
    Harness h;
    wo::ObjectType till;
    till.name = "dyn_cashreg";
    till.className = "dyn_cashreg";
    till.pickupAnim = 2;
    h.types.add(till);
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types, &h.messages);
    h.place(9, "dyn_cashreg", Vec3{10.0F, 11.0F, 1.4F});
    coney::LevelPickups::NativeUse answer = coney::LevelPickups::NativeUse::Lift;
    std::vector<double> asked;
    pickups.setNativeMessage([&](double object, double /*human*/) {
        asked.push_back(object);
        return answer;
    });
    coney::TriangleOutcome outcome = pickups.triangle(100, kFeet, kFacingY, false, {});
    CHECK(asked == std::vector<double>{9});
    REQUIRE(outcome.result == coney::TriangleResult::PickUp);
    CHECK(outcome.choice.handle == 9);
    CHECK(outcome.choice.clip == 504); // TwoHandPickUp, more than 0.8 m above the feet
    // Refused, the search does not take it though its class is pickable.
    answer = coney::LevelPickups::NativeUse::Refuse;
    outcome = pickups.triangle(100, kFeet, kFacingY, false, {});
    CHECK(outcome.result == coney::TriangleResult::Nothing);
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
    h.messages.setPrompt(11, "Keep", "Tag", "Spray the wall");
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
    CHECK(object.value_or(coney::ActionObject{}).hint == "Spray the wall");
    CHECK(pickups.triangle(100, kFeet, kFacingY, false, {}).result == coney::TriangleResult::Consumed);
    CHECK(h.touched == std::vector<double>{11});
    // Out of reach: 1.2 m away in the plane, or more than 1.5 m above the waist.
    CHECK_FALSE(pickups.actionObject(Vec3{9.8F, 10.0F, 0.0F}).has_value());
    CHECK_FALSE(pickups.actionObject(Vec3{10.5F, 10.0F, -1.4F}).has_value());
    CHECK(pickups.actionObject(Vec3{10.5F, 10.0F, -1.2F}).has_value());
}

TEST_CASE("a stolen stereo pays $15 and a car stereo, then calls the theft handler with the human and car",
          "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    std::vector<double> heard;
    h.scripts.vm().registerFunction("Stolen", [&heard](std::span<const Value> args) -> coney::script::binding::Results {
        for (const Value& arg : args) {
            heard.push_back(arg.number().value_or(-1.0));
        }
        return std::vector<Value>{};
    });
    h.state.player.stereoTheftHandler = "Stolen";
    pickups.stereoStolen(0, 181, 142);
    CHECK(h.state.player.inventory.count(0, item::kMoney) == coney::LevelPickups::kStereoMoney);
    CHECK(h.state.player.inventory.count(0, item::kCarStereo) == 1);
    CHECK(h.items == std::vector<double>{item::kMoney, item::kCarStereo});
    CHECK(heard == std::vector<double>{181, 142});
}

TEST_CASE("a won mugging pays all the victim's money; every end calls the mug callback", "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    std::vector<std::pair<double, bool>> heard;
    h.scripts.vm().registerFunction("Mugged", [&heard](std::span<const Value> args) -> coney::script::binding::Results {
        heard.emplace_back(args[0].number().value_or(-1.0), !args[1].isNil());
        return std::vector<Value>{};
    });
    int money = 18;
    pickups.mugEnded(181, "Mugged", false);
    CHECK(money == 18);
    pickups.mugPaid(0, money);
    CHECK(money == 0);
    pickups.mugEnded(181, "Mugged", true);
    CHECK(h.state.player.inventory.count(0, item::kMoney) == 18);
    CHECK(heard == std::vector<std::pair<double, bool>>{{181, false}, {181, true}});
}

TEST_CASE("SetInterrogateParam's set 0 overrides the mugging while its time is not 0", "[level_pickups]") {
    Harness h;
    const coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    CHECK_FALSE(pickups.muggingOverride().has_value());
    coney::InterrogateOverride& set = h.state.characters.interrogate.front();
    set.timesMs = {5000, 2500, 20000, 0};
    set.anglesRadians = {40.0F * std::numbers::pi_v<float> / 180.0F, 60.0F * std::numbers::pi_v<float> / 180.0F};
    const std::optional<coney::combat::MuggingParams> params = pickups.muggingOverride();
    REQUIRE(params.has_value());
    CHECK(params.value_or(coney::combat::MuggingParams{}).offTargetMs == 20000);
    CHECK(params.value_or(coney::combat::MuggingParams{}).toleranceDegrees == Catch::Approx(40.0F));
    CHECK(params.value_or(coney::combat::MuggingParams{}).gapDegrees == Catch::Approx(60.0F));
}

TEST_CASE("loot, a won mugging and a dealer's item play the item's pick-up sound once, without a position",
          "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    std::vector<std::uint32_t> played;
    pickups.setSound([&played](std::uint32_t hash) { played.push_back(hash); });
    h.state.player.inventory.configure(item::kMoney, "dyn_money", 0, "test/money_sound", 0);
    h.state.player.inventory.configure(item::kStolenLoot, "dyn_store_item", 0, "test/loot_sound", 0);
    h.state.player.inventory.configure(item::kRevive, "dyn_flash", 0, "none", 0);
    h.place(5, "dyn_watch", Vec3{10.0F, 11.0F, 0.3F});
    CHECK(pickups.take(5, 0) == coney::TakeResult::Loot);
    // The loot's sound alone: the money that comes with it is silent.
    CHECK(played == std::vector<std::uint32_t>{coney::crc32("test/loot_sound")});
    int money = 12;
    pickups.mugPaid(0, money);
    CHECK(played.back() == coney::crc32("test/money_sound"));
    // A victim with nothing pays nothing and plays nothing.
    played.clear();
    pickups.mugPaid(0, money);
    CHECK(played.empty());
    // An item whose sound is `none`, and a dirty dealer who kept the price, play nothing.
    pickups.dealerSold(0, item::kRevive, 1, 5);
    pickups.dealerSold(0, item::kMoney, 0, 5);
    CHECK(played.empty());
    pickups.dealerSold(0, item::kStolenLoot, 1, 5);
    CHECK(played == std::vector<std::uint32_t>{coney::crc32("test/loot_sound")});
}
