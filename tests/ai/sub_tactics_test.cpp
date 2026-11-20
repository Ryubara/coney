// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/sub_tactics.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/gangs.h"
#include "ai/tactic.h"

// The team-up framework (docs/research/ai-reimpl.md#unit-3): the pool of eight order records, an order's start, time
// limit and end, and a tactic's strategy slot that runs one order and hears when it ends. A fake order and a fake
// tactic stand for the real ones, whose work is Units 4 to 6; every step is logged so the order of calls shows.

using coney::ai::Gang;
using coney::ai::Gangs;
using coney::ai::OrderKind;
using coney::ai::SubTactic;
using coney::ai::SubTacticPool;
using coney::ai::Tactic;

namespace {

// What the fake orders and the fake tactic did, in order.
struct Log {
    int starts = 0;
    int processes = 0;
    int ends = 0;
    std::vector<std::string> events;
};

// An order of a chosen kind and limit that logs its hooks; its process says done when told to.
class FakeOrder final : public SubTactic {
  public:
    FakeOrder(Log& log, std::int64_t limitMs, OrderKind kind = OrderKind::Charge, std::string name = "order")
        : SubTactic(0, limitMs), m_log(&log), m_kind(kind), m_name(std::move(name)) {}
    [[nodiscard]] OrderKind kind() const override { return m_kind; }
    bool done = false; // what the class's process answers

  protected:
    void onStart(Gang& /*gang*/) override {
        ++m_log->starts;
        m_log->events.push_back(m_name + " start");
    }
    void onEnd(Gang& /*gang*/) override {
        ++m_log->ends;
        m_log->events.push_back(m_name + " end");
    }
    [[nodiscard]] bool onProcess(Gang& /*gang*/) override {
        ++m_log->processes;
        return done;
    }

  private:
    Log* m_log;
    OrderKind m_kind;
    std::string m_name;
};

// A tactic that does nothing but log the orders it hears end, and whether the record was still in the pool then.
class FakeTactic final : public Tactic {
  public:
    explicit FakeTactic(Log& log) : Tactic(0x30, ""), m_log(&log) {}
    void strategyEnded(Gang& gang, const SubTactic& order) override {
        // Read the slot once into a local, so the check and the use are of the same value.
        const std::optional<std::size_t> slot = strategy();
        const bool held = slot.has_value() && gang.owner().subTactics().at(*slot) == &order;
        m_log->events.push_back(held ? "heard, record held" : "heard, record gone");
    }

  private:
    Log* m_log;
};

// A pool with `count` fake orders of no limit in it.
void fill(SubTacticPool& pool, Log& log, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(pool.allocate(std::make_unique<FakeOrder>(log, -1)) == i);
    }
}

// A gang in use with the fake tactic set on it, started by one update at time 0.
FakeTactic& tacticOn(Gangs& gangs, Log& log, int& gang) {
    gang = gangs.create(3, "Riffs");
    auto tactic = std::make_unique<FakeTactic>(log);
    FakeTactic& made = *tactic;
    gangs.setTactic(gang, std::move(tactic));
    return made;
}

} // namespace

TEST_CASE("the order pool holds eight orders and a ninth allocation fails", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#the-order-record-and-pool-0x00321828-0x00321ab8, confirmed (code)
    Log log;
    SubTacticPool pool;
    CHECK(pool.inUse() == 0);
    fill(pool, log, 8);
    CHECK(pool.inUse() == 8);
    CHECK_FALSE(pool.allocate(std::make_unique<FakeOrder>(log, -1)).has_value());
    // A freed record is the first free one again; a slot outside the pool, or freed twice, is ignored.
    pool.free(3);
    pool.free(3);
    pool.free(99);
    CHECK(pool.at(3) == nullptr);
    CHECK(pool.inUse() == 7);
    CHECK(pool.allocate(std::make_unique<FakeOrder>(log, -1)) == std::optional<std::size_t>(3));
    CHECK(pool.at(99) == nullptr);
    pool.clear();
    CHECK(pool.inUse() == 0);
    CHECK(pool.allocate(std::make_unique<FakeOrder>(log, -1)) == std::optional<std::size_t>(0));
}

TEST_CASE("an order starts once, on its first process, with its limit made absolute", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#the-order-record-and-pool-0x00321828-0x00321ab8, confirmed (code)
    Log log;
    Gangs gangs;
    Gang& gang = *gangs.find(gangs.create(3, "Riffs"));
    FakeOrder order(log, 500);
    CHECK_FALSE(order.started());
    CHECK_FALSE(order.process(gang, 1000));
    CHECK(order.started());
    CHECK(order.endsAtMs() == 1500);
    CHECK_FALSE(order.process(gang, 1200));
    CHECK(log.starts == 1);
    CHECK(log.processes == 2);
}

TEST_CASE("an order is done once the game time is past its limit, not at it", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#the-order-record-and-pool-0x00321828-0x00321ab8, confirmed (code)
    Log log;
    Gangs gangs;
    Gang& gang = *gangs.find(gangs.create(3, "Riffs"));
    FakeOrder order(log, 500);
    order.start(gang, 1000);
    CHECK_FALSE(order.process(gang, 1500));
    CHECK(log.processes == 1);
    // Past the limit it is done without asking the class.
    CHECK(order.process(gang, 1501));
    CHECK(log.processes == 1);
    // Before then the class's answer stands.
    FakeOrder early(log, 500);
    early.done = true;
    CHECK(early.process(gang, 0));
}

TEST_CASE("an order with no limit never times out", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#the-order-record-and-pool-0x00321828-0x00321ab8, confirmed (code)
    Log log;
    Gangs gangs;
    Gang& gang = *gangs.find(gangs.create(3, "Riffs"));
    FakeOrder order(log, -1);
    CHECK_FALSE(order.process(gang, 1000));
    CHECK_FALSE(order.process(gang, 0xffffffffULL + 5));
    CHECK(log.processes == 2);
    order.finish(gang);
    CHECK(log.ends == 1);
}

TEST_CASE("a tactic ends a finished order, hears it before its record is freed and empties its slot",
          "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#the-strategy-slot-0x00306b50-0x00306b88-0x00306bd0, confirmed (code)
    Log log;
    Gangs gangs;
    int id = -1;
    FakeTactic& tactic = tacticOn(gangs, log, id);
    Gang& gang = *gangs.find(id);
    gangs.update(0); // a gang with no members runs no tactic: process it by hand
    REQUIRE(tactic.process(gang) == 0);
    const std::optional<std::size_t> slot = gangs.subTactics().allocate(std::make_unique<FakeOrder>(log, 1000));
    REQUIRE(slot.has_value());
    tactic.setStrategy(gang, slot);
    CHECK(tactic.strategy() == slot);
    // The first process starts it; within the limit it runs on.
    REQUIRE(tactic.process(gang) == 0);
    CHECK(log.starts == 1);
    CHECK(tactic.strategy() == slot);
    // Past the limit: its end, then the tactic hears it while the slot still names the record, then the record goes.
    gangs.update(1001);
    REQUIRE(tactic.process(gang) == 0);
    CHECK(log.events == std::vector<std::string>{"order start", "order end", "heard, record held"});
    CHECK_FALSE(tactic.strategy().has_value());
    CHECK(gangs.subTactics().inUse() == 0);
}

TEST_CASE("setting a strategy ends the order the tactic runs first", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#the-strategy-slot-0x00306b50-0x00306b88-0x00306bd0, confirmed (code)
    Log log;
    Gangs gangs;
    int id = -1;
    FakeTactic& tactic = tacticOn(gangs, log, id);
    Gang& gang = *gangs.find(id);
    SubTacticPool& pool = gangs.subTactics();
    tactic.setStrategy(gang, pool.allocate(std::make_unique<FakeOrder>(log, -1, OrderKind::Guard, "first")));
    const std::optional<std::size_t> second =
        pool.allocate(std::make_unique<FakeOrder>(log, -1, OrderKind::Throw, "second"));
    tactic.setStrategy(gang, second);
    CHECK(log.events == std::vector<std::string>{"first end", "heard, record held"});
    CHECK(tactic.strategy() == second);
    CHECK(pool.inUse() == 1);
}

TEST_CASE("an order the full pool could not take leaves the tactic's slot empty", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#unit-3, confirmed (code)
    Log log;
    Gangs gangs;
    int id = -1;
    FakeTactic& tactic = tacticOn(gangs, log, id);
    Gang& gang = *gangs.find(id);
    fill(gangs.subTactics(), log, 8);
    tactic.setStrategy(gang, gangs.subTactics().allocate(std::make_unique<FakeOrder>(log, -1)));
    CHECK_FALSE(tactic.strategy().has_value());
    // Processing an empty slot does nothing.
    REQUIRE(tactic.process(gang) == 0);
    CHECK(log.starts == 0);
}

TEST_CASE("a tactic's end ends and frees its order first", "[ai][tactics][teamup]") {
    // Expected: ai-reimpl.md#the-strategy-slot-0x00306b50-0x00306b88-0x00306bd0, confirmed (code)
    Log log;
    Gangs gangs;
    int id = -1;
    FakeTactic& tactic = tacticOn(gangs, log, id);
    tactic.setStrategy(*gangs.find(id), gangs.subTactics().allocate(std::make_unique<FakeOrder>(log, -1)));
    gangs.setTactic(id, nullptr);
    CHECK(log.events == std::vector<std::string>{"order end", "heard, record held"});
    CHECK(gangs.subTactics().inUse() == 0);
}
