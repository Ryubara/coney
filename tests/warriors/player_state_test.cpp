// SPDX-License-Identifier: GPL-3.0-or-later
// The players' state behind the inventory, statistics, unlockables, stopwatch and crime bindings
// (docs/research/player-state.md, docs/research/scripting.md#stopwatch, docs/research/ai.md#crimes). Synthetic values.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "core/pad_handlers.h"
#include "warriors/crime_reports.h"
#include "warriors/inventory.h"
#include "warriors/player_state.h"
#include "warriors/player_stats.h"
#include "warriors/profile_record.h"
#include "warriors/stop_watch.h"
#include "warriors/unlock_records.h"

using coney::CrimeGang;
using coney::CrimePosition;
using coney::CrimeReports;
using coney::Inventory;
using coney::PlayerStats;
using coney::StatCategory;
using coney::StopWatch;
using coney::UnlockRecord;
using coney::UnlockRecords;
namespace item = coney::item;
namespace crime = coney::crime;

TEST_CASE("the inventory clamps counts at 0 and revives at 3, or 4 with the upgrade", "[inventory]") {
    Inventory inventory;
    CHECK(inventory.give(0, item::kMoney, 25) == 25);
    CHECK(inventory.give(0, item::kMoney, -40) == -25);
    CHECK(inventory.count(0, item::kMoney) == 0);
    inventory.give(0, item::kRevive, 9);
    CHECK(inventory.count(0, item::kRevive) == 3);
    inventory.setReviveUpgrade(true);
    inventory.give(0, item::kRevive, 9);
    CHECK(inventory.count(0, item::kRevive) == 4);
    // The second player's inventory is its own; bad players and items read 0 and change nothing.
    CHECK(inventory.count(1, item::kRevive) == 0);
    CHECK(inventory.give(2, item::kMoney, 5) == 0);
    CHECK(inventory.give(0, 23, 5) == 0);
    CHECK(inventory.count(-1, item::kMoney) == 0);
    CHECK_FALSE(inventory.has(0, item::kHandcuffKey));
}

TEST_CASE("setting money tells the HUD and spray paint raises its hint once", "[inventory]") {
    Inventory inventory;
    inventory.setMoney(1, -5);
    CHECK(inventory.count(1, item::kMoney) == 0);
    CHECK(inventory.moneySets(1) == 1);
    CHECK(inventory.moneySets(0) == 0);
    inventory.setSprayPaint(0, 0);
    CHECK_FALSE(inventory.sprayHintPending());
    inventory.setSprayPaint(0, 3);
    CHECK(inventory.sprayHintPending());
    inventory.clearSprayHint();
    inventory.setSprayPaint(0, 5);
    CHECK_FALSE(inventory.sprayHintPending());
    CHECK(inventory.count(0, item::kSprayPaint) == 5);
}

TEST_CASE("CfgInventoryItem configures both players' slots", "[inventory]") {
    Inventory inventory;
    inventory.configure(6, "dyn_key", 0, "powerup", 30000);
    for (const int player : {0, 1}) {
        const coney::InventoryItem* slot = inventory.slot(player, 6);
        REQUIRE(slot != nullptr);
        CHECK(slot->objectName == "dyn_key");
        CHECK(slot->durationMs == 30000);
    }
    inventory.give(0, 6, 2);
    inventory.clearCounts();
    CHECK(inventory.count(0, 6) == 0);
    CHECK(inventory.slot(0, 6)->objectName == "dyn_key");
}

TEST_CASE("a category scores count times points; harmony is the maximum less its points", "[stats]") {
    PlayerStats stats;
    stats.setPoints(0, 0, 1000);
    stats.setPoints(3, 1, 4);
    stats.setPoints(5, 2, 200);
    stats.setPoints(0, 3, 9); // past mission's three events: ignored
    CHECK(stats.points(0, 3) == 0);
    stats.setMaximum(StatCategory::Harmony, 4000);
    stats.setMaximum(StatCategory::Combat, 10);

    stats.add(0, 0, 0);
    stats.add(0, 3, 1, 5);
    stats.add(0, 5, 2, 2);
    CHECK(stats.categoryPoints(0, StatCategory::Combat) == 20);
    CHECK(stats.categoryPercent(0, StatCategory::Combat) == 100);
    CHECK(stats.categoryScore(0, StatCategory::Harmony) == 3600);
    CHECK(stats.categoryPercent(0, StatCategory::Harmony) == 90);
    // Coney's score: the five scoring categories less harmony's points.
    CHECK(stats.score(0) == 1000 + 20 - 400);
    CHECK(stats.score(1) == 0);

    stats.resetPlayer(0);
    CHECK(stats.count(0, 3, 1) == 0);
    CHECK(stats.points(3, 1) == 4); // the points stay
}

TEST_CASE("the stopwatch counts down by game time, stops at its target and beeps once a second near it",
          "[stopwatch]") {
    StopWatch watch;
    watch.set(5000, 0, "P1.TimesUp");
    watch.setWarning(3000);
    CHECK_FALSE(watch.running());
    CHECK_FALSE(watch.step(100).finished); // not running: nothing moves
    CHECK(watch.time() == 5000);

    watch.start(true, 1000);
    CHECK_FALSE(watch.step(2000).beep);
    CHECK(watch.time() == 4000);
    CHECK(watch.step(3000).beep); // 3000: inside the window
    CHECK_FALSE(watch.step(3500).beep);
    CHECK(watch.step(4000).beep); // a second after the last beep

    // Stopped for a while: a resumed watch does not count the time it was stopped.
    watch.start(false, 4000);
    watch.start(true, 9000);
    watch.step(9500);
    CHECK(watch.time() == 1500);

    const StopWatch::Step end = watch.step(20000);
    CHECK(end.finished);
    CHECK(watch.time() == 0);
    CHECK_FALSE(watch.running());
    CHECK(watch.callback() == "P1.TimesUp");
}

TEST_CASE("the stopwatch counts up to a target above it", "[stopwatch]") {
    StopWatch watch;
    watch.set(0, 1000, "Done");
    watch.start(true, 0);
    CHECK_FALSE(watch.step(999).finished);
    CHECK(watch.step(1001).finished);
    CHECK(watch.time() == 1000);
}

TEST_CASE("UM_Unlock unlocks matching records into the profile's bits and marks them new", "[unlocks]") {
    UnlockRecords records;
    coney::SavedProgress progress;
    records.setCount(4);
    CHECK(records.set(0, UnlockRecord{.level = 99, .group = 0, .item = 0, .type = 0, .extra = 0, .data = 80}));
    CHECK(records.set(1, UnlockRecord{.level = 99, .group = 0, .item = 0, .type = 6, .extra = 0, .data = 7}));
    CHECK(records.set(2, UnlockRecord{.level = 80, .group = 0, .item = 0, .type = 1, .extra = 0, .data = 12}));
    CHECK_FALSE(records.set(4, UnlockRecord{}));

    CHECK_FALSE(records.isLevelComplete(progress, 99));
    CHECK(records.unlock(progress, 99, 0, 0) == 2);
    CHECK(records.unlock(progress, 99, 0, 0) == 0); // already unlocked
    CHECK(records.isLevelComplete(progress, 99));
    CHECK_FALSE(records.isLevelComplete(progress, 80));
    CHECK(records.isDataUnlocked(progress, 6, 7));
    CHECK_FALSE(records.isDataUnlocked(progress, 1, 12));
    CHECK_FALSE(progress.isLocked(0));

    CHECK(records.isDataDirty(progress, 6, 7, false));
    CHECK(records.isTypeDirty(progress, 6, true));
    CHECK_FALSE(records.isTypeDirty(progress, 6, true));
    CHECK(coney::isMarkedNew(progress, 0));

    // A reset drops the records, not the profile's bits.
    records.reset();
    CHECK(records.records().empty());
    CHECK_FALSE(progress.isLocked(0));
}

namespace {

// Crime services that record what the report asks of them.
struct RecordingServices final : coney::CrimeServices {
    std::optional<CrimeGang> gang = CrimeGang{.id = 3, .kind = 0};
    int playerGang = 3;
    std::vector<std::string> calls;

    std::optional<CrimeGang> gangOf(double /*handle*/) override { return gang; }
    bool isPlayer(double handle) override { return handle == 1.0; }
    int playerOneGang() override { return playerGang; }
    void makePoliceHostile(int gangId, bool mutual) override {
        calls.push_back("hostile " + std::to_string(gangId) + (mutual ? " both" : " one"));
    }
    void clearPoliceHostility(int gangId) override { calls.push_back("calm " + std::to_string(gangId)); }
    void callCrimeCallback(const std::string& function, int gangId, int type) override {
        calls.push_back(function + " " + std::to_string(gangId) + " " + std::to_string(type));
    }
    void moveCrimeScene(const CrimePosition& /*at*/) override { calls.emplace_back("scene"); }
    void queueResponders(int type, int kind, int count, const CrimePosition& /*at*/, double delay) override {
        calls.push_back("respond " + std::to_string(type) + " " + std::to_string(kind) + " " + std::to_string(count) +
                        " " + std::to_string(static_cast<int>(delay)));
    }
    void markStoreRobbed(const CrimePosition& /*at*/, int gangId) override {
        calls.push_back("robbed " + std::to_string(gangId));
    }
    void scoreAssault(double /*offender*/, double /*victim*/) override { calls.emplace_back("assault"); }
    void notifyHud(int message) override { calls.push_back("hud " + std::to_string(message)); }
};

} // namespace

TEST_CASE("a crime report turns the police on the gang, sends responders and starts 10 s of wanted", "[crimes]") {
    CrimeReports crimes;
    RecordingServices services;
    crimes.setResponders(crime::kAssault, 2);
    crimes.setCallback("OnCrime");
    const CrimePosition at{1, 2, 3};

    crimes.report(services, crime::kAssault, at, 1.0, 50.0, true, 0, 1000);
    CHECK(services.calls ==
          std::vector<std::string>{"hostile 3 both", "OnCrime 3 0", "scene", "respond 0 1 2 0", "assault", "hud 0"});
    CHECK(crimes.wanted(3));
    CHECK(crimes.lastCrime() == crime::kAssault);
    CHECK(crimes.lastAssault(3) == 1000);
    CHECK(crimes.wantedFraction(3, 6000) == 0.5F);

    // The same victim scores once; the scene does not move again to the same place.
    services.calls.clear();
    crimes.report(services, crime::kAssault, at, 1.0, 50.0, false, 0, 2000);
    CHECK(services.calls == std::vector<std::string>{"hostile 3 both", "OnCrime 3 0", "hud 0"});

    // Wanted runs out 10 s after the last report.
    services.calls.clear();
    crimes.update(services, 11999);
    CHECK(crimes.wanted(3));
    crimes.update(services, 12000);
    CHECK_FALSE(crimes.wanted(3));
    CHECK(services.calls == std::vector<std::string>{"calm 3", "hud 11"});
    CHECK(crimes.lastCrime() == crime::kNoCrime);
}

TEST_CASE("crime reports: off, police offenders, one-way types, break-ins and custom counts", "[crimes]") {
    CrimeReports crimes;
    RecordingServices services;
    const CrimePosition at{0, 0, 0};

    crimes.setReporting(false);
    crimes.report(services, crime::kAssault, at, 1.0, 0.0, true, 0, 0);
    CHECK(services.calls.empty());
    crimes.setReporting(true);

    services.gang = CrimeGang{.id = 4, .kind = 1};
    crimes.report(services, crime::kAssault, at, 7.0, 0.0, true, 0, 0);
    CHECK(services.calls.empty());

    services.gang = CrimeGang{.id = 3, .kind = 0};
    crimes.report(services, crime::kTrespassing, at, 1.0, 0.0, true, 0, 0);
    CHECK(services.calls.front() == "hostile 3 one");
    CHECK(crimes.lastCrime() == crime::kTrespassing);
    // 12 is kept over a later crime.
    crimes.report(services, crime::kTheft, at, 1.0, 0.0, true, 0, 0);
    CHECK(crimes.lastCrime() == crime::kTrespassing);

    services.calls.clear();
    crimes.setResponders(crime::kBreakAndEnter, 2);
    crimes.setBreakInDelay(15.0);
    crimes.report(services, crime::kBreakAndEnter, CrimePosition{5, 0, 0}, 1.0, 0.0, true, 0, 0);
    CHECK(services.calls.at(1) == "scene");
    CHECK(services.calls.at(2) == "respond 1 1 2 15");
    CHECK(services.calls.at(3) == "robbed 3");

    services.calls.clear();
    crimes.report(services, crime::kCustom, at, 0.0, 0.0, true, 6, 0);
    CHECK(services.calls == std::vector<std::string>{"scene", "respond 4 1 6 0"});
    crimes.report(services, crime::kTagging, at, 0.0, 0.0, true, 0, 0);
    CHECK(services.calls.size() == 2); // tagging sends none
}

TEST_CASE("forcing the crime level holds a gang wanted", "[crimes]") {
    CrimeReports crimes;
    RecordingServices services;
    crimes.report(services, crime::kTheft, CrimePosition{}, 1.0, 0.0, false, 0, 0);
    crimes.setForced(true);
    crimes.update(services, 50000);
    CHECK(crimes.wanted(3));
    crimes.setForced(false);
    crimes.update(services, 59999); // held at 10 s from the last forced update
    CHECK(crimes.wanted(3));
    crimes.update(services, 60000);
    CHECK_FALSE(crimes.wanted(3));
}

TEST_CASE("pad handlers are kept under the highest bit of the mask", "[pad_handlers]") {
    coney::PadHandlers handlers;
    handlers.set(0, coney::pad::kCross | coney::pad::kSquare, "OnSquare");
    handlers.set(0, coney::pad::kL2, "OnL2");
    handlers.set(4, coney::pad::kStart, "OnStart");
    handlers.set(8, coney::pad::kStart, "Nowhere");
    CHECK(handlers.handler(0, 7) == "OnSquare");
    CHECK(handlers.handler(0, 6).empty());
    CHECK(handlers.due(0, coney::pad::kSquare | coney::pad::kL2) == std::vector<std::string>{"OnL2", "OnSquare"});
    CHECK(handlers.due(4, coney::pad::kStart) == std::vector<std::string>{"OnStart"});
    handlers.set(0, coney::pad::kL2, "");
    CHECK(handlers.due(0, coney::pad::kL2).empty());
    handlers.clear();
    CHECK(handlers.due(4, 0xffff).empty());
}

TEST_CASE("a checkpoint copy restores the inventories and statistics", "[player_state]") {
    coney::PlayerState state;
    state.inventory.give(0, item::kMoney, 40);
    state.stats.add(0, 3, 0, 2);
    state.saveCheckpoint();
    state.inventory.give(0, item::kMoney, 60);
    state.stats.add(0, 3, 0, 5);
    state.restoreCheckpoint();
    CHECK(state.inventory.count(0, item::kMoney) == 40);
    CHECK(state.stats.count(0, 3, 0) == 2);
}
