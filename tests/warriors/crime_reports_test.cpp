// SPDX-License-Identifier: GPL-3.0-or-later
// The crime report and the wanted timers as the original's code has them (docs/research/crimes.md#break-in-police,
// docs/research/crimes.md#report-types, docs/research/crimes.md#wanted): the break-in's store test, the assault time,
// the victim's score, a custom crime's count and what ForceCrimeLevel holds. Synthetic services.
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "warriors/crime_reports.h"

using coney::CrimeGang;
using coney::CrimePosition;
using coney::CrimeReports;
using coney::CrimeStore;
namespace crime = coney::crime;

namespace {

// Services that record what the report asks for, with one offender gang, one store and set brain kinds and players.
class StoreServices final : public coney::CrimeServices {
  public:
    std::optional<CrimeGang> gang = CrimeGang{.id = 3, .kind = 0};
    std::optional<CrimeStore> store;
    std::optional<double> leader;
    std::map<double, int> players;    // handle -> player index
    std::map<double, int> brainTypes; // handle -> brain kind
    std::vector<std::string> calls;

    std::optional<CrimeGang> gangOf(double /*handle*/) override { return gang; }
    int playerIndex(double handle) override {
        const auto found = players.find(handle);
        return found != players.end() ? found->second : -1;
    }
    int brainType(double handle) override {
        const auto found = brainTypes.find(handle);
        return found != brainTypes.end() ? found->second : -1;
    }
    std::optional<CrimeStore> nearestStore(const CrimePosition& /*at*/) override { return store; }
    void robStore(const CrimePosition& /*at*/) override { calls.emplace_back("rob"); }
    void claimStore(const CrimePosition& /*at*/, int gangId) override {
        calls.push_back("claim " + std::to_string(gangId));
    }
    std::optional<double> gangLeader(double /*offender*/) override { return leader; }
    void queueResponders(int type, int kind, int count, const CrimePosition& /*at*/, double delay) override {
        calls.push_back("respond " + std::to_string(type) + " " + std::to_string(kind) + " " + std::to_string(count) +
                        " " + std::to_string(static_cast<int>(delay)));
    }
    void scoreCrime(double human, int event) override {
        calls.push_back("crime " + std::to_string(static_cast<int>(human)) + " " + std::to_string(event));
    }
};

// Whether `calls` holds `call`.
bool has(const std::vector<std::string>& calls, const std::string& call) {
    return std::ranges::find(calls, call) != calls.end();
}

} // namespace

TEST_CASE("a break-in calls the police only at a store within 10 m that is not yet robbed", "[crimes]") {
    // Expected: crimes.md#break-in-police, confirmed (code)
    CrimeReports crimes;
    StoreServices services;
    crimes.setResponders(crime::kBreakAndEnter, 2);
    crimes.setBreakInDelay(15.0);
    const CrimePosition at{5, 0, 0};

    // No store front within 10 m: no police and nothing robbed.
    crimes.report(services, crime::kBreakAndEnter, at, 1.0, 0.0, true, 0, 0);
    CHECK_FALSE(has(services.calls, "respond 1 1 2 15"));
    CHECK_FALSE(has(services.calls, "rob"));

    // A store not yet robbed: the responders after the break-in delay, then the store robbed.
    services.calls.clear();
    services.store = CrimeStore{.robbed = false};
    crimes.report(services, crime::kBreakAndEnter, at, 1.0, 0.0, true, 0, 0);
    CHECK(has(services.calls, "respond 1 1 2 15"));
    CHECK(has(services.calls, "rob"));

    // A store robbed already: no police.
    services.calls.clear();
    services.store = CrimeStore{.robbed = true, .gang = 5};
    crimes.report(services, crime::kBreakAndEnter, at, 1.0, 0.0, true, 0, 0);
    CHECK_FALSE(has(services.calls, "respond 1 1 2 15"));
    CHECK_FALSE(has(services.calls, "rob"));
}

TEST_CASE("a robbed store with no gang yet goes to the offender's gang and scores its leader crime event 1",
          "[crimes]") {
    // Expected: crimes.md#break-in-police, confirmed (code)
    CrimeReports crimes;
    StoreServices services;
    services.store = CrimeStore{.robbed = true, .gang = CrimeStore::kNoGang};
    services.leader = 9.0;
    services.players[9.0] = 0;
    crimes.report(services, crime::kBreakAndEnter, CrimePosition{}, 1.0, 0.0, true, 0, 0);
    CHECK(has(services.calls, "claim 3"));
    CHECK(has(services.calls, "crime 9 1"));

    // A leader who is not a player scores nothing; a store some gang holds already is left alone.
    services.calls.clear();
    services.players.clear();
    crimes.report(services, crime::kBreakAndEnter, CrimePosition{}, 1.0, 0.0, true, 0, 0);
    CHECK(has(services.calls, "claim 3"));
    CHECK_FALSE(has(services.calls, "crime 9 1"));
    services.calls.clear();
    services.store = CrimeStore{.robbed = true, .gang = 5};
    crimes.report(services, crime::kBreakAndEnter, CrimePosition{}, 1.0, 0.0, true, 0, 0);
    CHECK_FALSE(has(services.calls, "claim 3"));
}

TEST_CASE("the offender gang's assault time is noted only when the report sends responders", "[crimes]") {
    // Expected: crimes.md#report-types, confirmed (code)
    CrimeReports crimes;
    StoreServices services;
    crimes.report(services, crime::kAssault, CrimePosition{}, 1.0, 0.0, false, 0, 1000);
    CHECK_FALSE(crimes.lastAssault(3).has_value());
    crimes.report(services, crime::kCopKill, CrimePosition{}, 1.0, 0.0, false, 0, 1500);
    CHECK_FALSE(crimes.lastAssault(3).has_value());
    crimes.report(services, crime::kMugging, CrimePosition{}, 1.0, 0.0, true, 0, 2000);
    CHECK(crimes.lastAssault(3) == 2000);
}

TEST_CASE("a player's victim scores for a cop (event 3) or a civilian or dealer (event 0), once per victim and player",
          "[crimes]") {
    // Expected: crimes.md#report-types, confirmed (code)
    CrimeReports crimes;
    StoreServices services;
    services.players[1.0] = 0;
    services.players[2.0] = 1;
    services.brainTypes[50.0] = 1; // a cop
    services.brainTypes[51.0] = 4; // a civilian
    services.brainTypes[52.0] = 5; // a dealer
    services.brainTypes[53.0] = 2; // a gang member: no score
    crimes.report(services, crime::kAssault, CrimePosition{}, 1.0, 50.0, false, 0, 0);
    crimes.report(services, crime::kAssault, CrimePosition{}, 1.0, 51.0, false, 0, 0);
    crimes.report(services, crime::kMugging, CrimePosition{}, 1.0, 52.0, false, 0, 0);
    crimes.report(services, crime::kCopAssault, CrimePosition{}, 1.0, 53.0, false, 0, 0);
    CHECK(services.calls == std::vector<std::string>{"crime 1 3", "crime 1 0", "crime 1 0"});

    // The same victim again: player 1 scores no more, player 2 still scores once.
    services.calls.clear();
    crimes.report(services, crime::kAssault, CrimePosition{}, 1.0, 50.0, false, 0, 0);
    crimes.report(services, crime::kAssault, CrimePosition{}, 2.0, 50.0, false, 0, 0);
    crimes.report(services, crime::kAssault, CrimePosition{}, 2.0, 50.0, false, 0, 0);
    CHECK(services.calls == std::vector<std::string>{"crime 2 3"});
}

TEST_CASE("a custom crime with a count of 0 queues no responders", "[crimes]") {
    // Expected: crimes.md#report-types, confirmed (code)
    CrimeReports crimes;
    StoreServices services;
    crimes.report(services, crime::kCustom, CrimePosition{}, 0.0, 0.0, true, 0, 0);
    CHECK(services.calls.empty());
    crimes.report(services, crime::kCustom, CrimePosition{}, 0.0, 0.0, true, 6, 0);
    CHECK(services.calls == std::vector<std::string>{"respond 4 1 6 0"});
}

TEST_CASE("ForceCrimeLevel holds a wanted gang's second timer too", "[crimes]") {
    // Expected: crimes.md#wanted, confirmed (code)
    CrimeReports crimes;
    StoreServices services;
    crimes.report(services, crime::kTheft, CrimePosition{}, 1.0, 0.0, false, 0, 0);
    crimes.setSecondWanted(3, 0);
    crimes.setSecondWanted(4, 0); // a gang that is not wanted: its second timer runs out as ever
    crimes.setForced(true);
    crimes.update(services, 20000);
    CHECK(crimes.wanted(3));
    CHECK(crimes.secondWanted(3));
    CHECK_FALSE(crimes.secondWanted(4));
}

TEST_CASE("a wanted timer runs out only once the game time is past its end", "[crimes]") {
    // Expected: crimes.md#wanted, confirmed (code)
    CrimeReports crimes;
    StoreServices services;
    crimes.report(services, crime::kTheft, CrimePosition{}, 1.0, 0.0, false, 0, 0);
    crimes.setSecondWanted(4, 0);
    crimes.update(services, CrimeReports::kWantedMs);
    CHECK(crimes.wanted(3));
    CHECK(crimes.secondWanted(4));
    crimes.update(services, CrimeReports::kWantedMs + 1);
    CHECK_FALSE(crimes.wanted(3));
    CHECK_FALSE(crimes.secondWanted(4));
}
