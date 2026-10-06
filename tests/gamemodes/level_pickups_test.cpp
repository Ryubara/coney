// SPDX-License-Identifier: GPL-3.0-or-later
// A level's loose objects as a player picks them up (docs/research/combat.md#breakables): the search over the spawn
// records, and the take, which adds a TYPE_SPECIAL's loot and money through the inventory callbacks.
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

// The bindings over a game state, a watch type (loot worth 7) and a crate type, and a native `Item` recording the
// inventory callback's items.
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
    std::vector<double> items;

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
        const std::vector<Value> callback{Value(std::string("Item"))};
        REQUIRE(scripts.vm().call(scripts.vm().global("CfgInventoryCallback"), callback).has_value());
        wo::ObjectType watch;
        watch.name = "dyn_watch";
        watch.className = "pickup_item";
        watch.value = 7;
        watch.objectKind = wo::kObjectKindSpecial;
        types.add(watch);
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
    CHECK(choice->handle == 2);
    CHECK(choice->clip == wo::kPickupHighClip);
}

TEST_CASE("taking loot adds item 10 with the callback, then its value in money, and removes it", "[level_pickups]") {
    Harness h;
    coney::LevelPickups pickups(h.scripts, h.state, h.records, h.types);
    h.place(5, "dyn_watch", Vec3{10.0F, 11.0F, 0.3F});
    h.place(6, "crate", Vec3{10.0F, 11.0F, 0.3F});
    CHECK(pickups.take(5, 0));
    CHECK(h.state.player.inventory.count(0, item::kStolenLoot) == 1);
    CHECK(h.state.player.inventory.count(0, item::kMoney) == 7);
    // The money goes in without notify: the callback heard the loot alone.
    CHECK(h.items == std::vector<double>{item::kStolenLoot});
    CHECK(h.records.find(5)->removed);
    CHECK_FALSE(pickups.take(5, 0));
    // Any other kind is only removed.
    CHECK(pickups.take(6, 0));
    CHECK(h.records.find(6)->removed);
    CHECK(h.state.player.inventory.count(0, item::kStolenLoot) == 1);
    CHECK_FALSE(pickups.take(99, 0));
}
