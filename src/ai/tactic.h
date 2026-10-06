// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "ai/brain.h"

// A gang's tactic: a plan for the whole gang (watch a fight as a crowd, defend, follow the war chief). While a gang
// has one, its members' fights push no fight goal: the tactic fights for them. It starts on its first process, which
// flushes every AI member; it may have a time limit and a Lua callback the gang update fires with what its process
// returns. The types below `0x12` leave the members their own goals.
// Research: docs/research/ai.md#tactics

namespace coney::ai {

class Gang;

/// Tactics of types below this leave the members their own goals (vtable `+0x3c`).
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
    /// Whether the members keep their own goals (vtable `+0x3c`): true for the types below kFirstGangTactic.
    [[nodiscard]] bool keepsOwnGoals() const { return m_type < kFirstGangTactic; }
    /// Whether it has started (`+0x04`).
    [[nodiscard]] bool started() const { return m_started; }
    /// Its Lua callback (`+0x0c`).
    [[nodiscard]] const std::string& callback() const { return m_callback; }

    /// One gang update (`Tactic_Process`): starts it on the first (Tactic_Start: the time limit made absolute, every
    /// AI member of `gang` flushed, then start()); past the time limit 2; else what update() returns.
    /// @orig 0x003067d8 Tactic_Process (unknown)
    /// @orig 0x00306690 Tactic_Start (unknown)
    int process(Gang& gang);
    /// Fires the Lua callback with the gang's id and `code` (vtable `+0x54`), when there is one and a script state.
    /// @orig 0x00306938 Tactic_FireCallback (unknown)
    void fireCallback(Gang& gang, int code);

    /// Its start (vtable `+0x0c`), after the members were flushed.
    virtual void start(Gang& /*gang*/) {}
    /// Its end (vtable `+0x14`), when it is cleared or replaced.
    virtual void end(Gang& /*gang*/) {}
    /// One update's work (vtable `+0x24`); non-zero fires the callback.
    [[nodiscard]] virtual int update(Gang& /*gang*/) { return 0; }
    /// An event to `member` (vtable `+0x4c`), after the gang's message handlers; true when it used it.
    virtual bool event(Gang& /*gang*/, Brain& /*member*/, const BrainEvent& /*event*/) { return false; }

  private:
    int m_type;
    std::string m_callback;
    std::int64_t m_timeLimitMs; // +0x08
    std::uint64_t m_endsAtMs = 0;
    bool m_started = false; // +0x04
};

} // namespace coney::ai
