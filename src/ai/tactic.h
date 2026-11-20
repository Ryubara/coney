// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "ai/brain.h"

// A gang's tactic: a plan for the whole gang (watch a fight as a crowd, defend, follow the war chief). While a gang
// has one, its members' fights push no fight goal: the tactic fights for them. It starts on its first process, which
// marks every AI member's goal base: the tactic's goals go on top of his own, and its end pops back down to them. It
// may have a time limit and a Lua callback the gang update fires with what its process returns, and a strategy slot
// that runs one team-up order (SubTactic) at a time.
// Research: docs/research/ai-gangs.md#tactics, docs/research/ai-reimpl.md#unit-3

namespace coney::ai {

class Gang;
class SubTactic;

/// Tactics of types below this answer 1 at vtable `+0x3c` (alertsGang()).
inline constexpr int kFirstGangTactic = 0x12;

/// A tactic. Subclasses override what they do; every hook does nothing by default.
class Tactic {
  public:
    /// A tactic of type `type` whose Lua callback is `callback` (empty for none), lasting `timeLimitMs` from its start
    /// (negative: no limit).
    /// @orig 0x003068f8 Tactic_SetCallback (unknown)
    Tactic(int type, std::string callback, std::int64_t timeLimitMs = -1)
        : m_type(type), m_callback(std::move(callback)), m_timeLimitMs(timeLimitMs) {}
    Tactic(const Tactic&) = delete;
    Tactic& operator=(const Tactic&) = delete;
    Tactic(Tactic&&) = delete;
    Tactic& operator=(Tactic&&) = delete;
    virtual ~Tactic() = default;

    /// Its type id (vtable `+0x34`).
    [[nodiscard]] int type() const { return m_type; }
    /// The vtable `+0x3c` flag, true for the types below kFirstGangTactic: its start puts the gang in the fighting
    /// state (any other tactic's start calms it), and while
    /// it is false a new enemy is offered to the tactic (event `0xb`). It gates no goal handling.
    [[nodiscard]] bool alertsGang() const { return m_type < kFirstGangTactic; }
    /// Whether it has started (`+0x04`).
    [[nodiscard]] bool started() const { return m_started; }
    /// Its Lua callback (`+0x0c`).
    [[nodiscard]] const std::string& callback() const { return m_callback; }

    /// One gang update (`Tactic_Process`): starts it on the first (Tactic_Start: the time limit made absolute, the gang
    /// in the fighting state when alertsGang() and calm otherwise (Gang::setAlertState()), every AI member's goal base
    /// marked and his target dropped, then start()); past the time limit 2; else the strategy's update
    /// (updateStrategy()), then what update() returns.
    /// @orig 0x003067d8 Tactic_Process (unknown)
    /// @orig 0x00306690 Tactic_Start (unknown)
    int process(Gang& gang);
    /// Fires the Lua callback with the gang's id and `code` (vtable `+0x54`), when there is one and a script state.
    /// @orig 0x00306938 Tactic_FireCallback (unknown)
    void fireCallback(Gang& gang, int code);

    /// Its start (vtable `+0x0c`), after the members' goal bases were marked.
    virtual void start(Gang& /*gang*/) {}
    /// The end (`Tactic_End`): the strategy ended (endStrategy()), every AI member popped to his goal base, then end().
    /// The gangs call it when the tactic is cleared or replaced.
    /// @orig 0x00306850 Tactic_End (unknown)
    void finish(Gang& gang);
    /// Its end (vtable `+0x14`), when it is cleared or replaced, after the members were popped to their goal bases.
    virtual void end(Gang& /*gang*/) {}
    /// One update's work (vtable `+0x24`); non-zero fires the callback.
    [[nodiscard]] virtual int update(Gang& /*gang*/) { return 0; }
    /// An event to `member` (vtable `+0x4c`), after the gang's message handlers; true when it used it.
    virtual bool event(Gang& /*gang*/, Brain& /*member*/, const BrainEvent& /*event*/) { return false; }

    /// The strategy slot (`+0x10`): the pool record (Gangs::subTactics()) of the order it runs; nothing for none.
    [[nodiscard]] std::optional<std::size_t> strategy() const { return m_strategy; }
    /// Runs `order` (a record of the gang's pool, or nothing when the pool was full) after ending the one it runs.
    /// @orig 0x00306b50 Tactic_SetStrategy (unknown)
    void setStrategy(Gang& gang, std::optional<std::size_t> order);
    /// The order's update (SubTactic::process()), from every process(): ends it (endStrategy()) once it is done.
    /// @orig 0x00306b88 Tactic_UpdateStrategy (unknown)
    void updateStrategy(Gang& gang);
    /// Ends the order it runs, if any: the order's end (SubTactic::finish()), then strategyEnded() while the slot still
    /// names it, then the record freed and the slot emptied.
    /// @orig 0x00306bd0 Tactic_EndStrategy (unknown)
    void endStrategy(Gang& gang);
    /// Hears that `order` ended (vtable `+0x1c`), before its record is freed.
    virtual void strategyEnded(Gang& /*gang*/, const SubTactic& /*order*/) {}

  private:
    int m_type;
    std::string m_callback;
    std::int64_t m_timeLimitMs; // +0x08
    std::uint64_t m_endsAtMs = 0;
    bool m_started = false;                // +0x04
    std::optional<std::size_t> m_strategy; // +0x10
};

} // namespace coney::ai
