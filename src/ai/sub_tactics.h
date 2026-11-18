// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

// The team-up orders a tactic runs in its strategy slot (Tactic::strategy()): a leader tells part of his gang to
// charge, guard him, go for the enemy leader, grab or throw. An order is a record of the shared pool of eight, holds
// its gang and a time limit, starts on its first process and is done once the limit has passed or its own process says
// so; its tactic then ends it, hears of it and frees the record. Research: docs/research/ai-reimpl.md#unit-3,
// docs/research/ai-code.md#t2-sub-tactics

namespace coney::ai {

class Gang;

/// How many order records the pool holds (8 of `0x30` at `0x006eb6f0`).
inline constexpr std::size_t kSubTacticSlots = 8;

/// The orders' kinds (their vtable slot `+0x2c`), and the draw's numbers for them (AttackTactic_StartSubTactic).
enum class OrderKind : std::uint8_t {
    Charge = 0, ///< `0x00544290`: count members charge their farthest enemy.
    Guard = 1,  ///< `0x005442c8`: the members guard the leader (draws 1 and 2, 2 backing off).
    Leader = 4, ///< `0x00544300`: count members go for the enemy gang's leader.
    Throw = 5,  ///< `0x00544338`: count members throw things (draws 3 and 5, 3 from a pile).
    Grab = 6,   ///< `0x00544220`: the leader grabs a dog.
};

/// A team-up order (a sub-tactic): the record's gang (`+0x00`), time limit (`+0x04`) and started byte (`+0x08`), and
/// the class's hooks.
class SubTactic {
  public:
    /// An order for gang `gang` (its id) lasting `limitMs` from its start; negative: no limit.
    SubTactic(int gang, std::int64_t limitMs) : m_gang(gang), m_limitMs(limitMs) {}
    SubTactic(const SubTactic&) = delete;
    SubTactic& operator=(const SubTactic&) = delete;
    SubTactic(SubTactic&&) = delete;
    SubTactic& operator=(SubTactic&&) = delete;
    virtual ~SubTactic() = default;

    /// Its kind (vtable `+0x2c`); a guard order's ending gives the attack tactic's Melee again.
    [[nodiscard]] virtual OrderKind kind() const = 0;
    /// The gang it orders (`+0x00`), by id.
    [[nodiscard]] int gang() const { return m_gang; }
    /// Whether it has started (`+0x08`).
    [[nodiscard]] bool started() const { return m_started; }
    /// The time limit as made: relative ms, negative for none.
    [[nodiscard]] std::int64_t limitMs() const { return m_limitMs; }
    /// The game time it is done after, once started; the largest time while it has no limit.
    [[nodiscard]] std::uint64_t endsAtMs() const { return m_endsAtMs; }

    /// The start at game time `nowMs`: a limit made absolute, the class's start (onStart()), then marked started.
    /// @orig 0x003219a0 SubTactic_Start (unknown)
    void start(Gang& gang, std::uint64_t nowMs);
    /// One update at `nowMs`: started first when it has not; done (true) once `nowMs` is past the limit, else what the
    /// class's process (onProcess()) says.
    /// @orig 0x00321a00 SubTactic_Process (unknown)
    [[nodiscard]] bool process(Gang& gang, std::uint64_t nowMs);
    /// The end: the class's end (onEnd()).
    /// @orig 0x00321a70 SubTactic_End (unknown)
    void finish(Gang& gang);

  protected:
    /// The class's start (vtable `+0x0c`).
    virtual void onStart(Gang& /*gang*/) {}
    /// The class's end (vtable `+0x14`).
    virtual void onEnd(Gang& /*gang*/) {}
    /// The class's process (vtable `+0x1c`): true when done.
    [[nodiscard]] virtual bool onProcess(Gang& /*gang*/) { return false; }

  private:
    int m_gang;                   // +0x00
    std::int64_t m_limitMs;       // +0x04 until started
    std::uint64_t m_endsAtMs = 0; // +0x04 once started
    bool m_started = false;       // +0x08
};

/// The pool of order records (`0x006eb6f0`, its in-use mask `0x006eb6e0`), owned by the level's gangs. A record is
/// named by its slot, 0 to kSubTacticSlots - 1.
class SubTacticPool {
  public:
    /// An empty pool: the mask the executable's static initialiser clears at start-up.
    /// @orig 0x00321a98 SubTactic_Reset (unknown)
    /// @orig 0x00321ab8 SubTactic_ResetAll (unknown)
    SubTacticPool();

    /// Every record free (the mask cleared; Game_InitializeSubsystems does it, and Coney's gangs when made).
    /// @orig 0x00321828 SubTactic_PoolClear (unknown)
    void clear();
    /// Puts `order` in the first free record and returns its slot; nothing, and `order` dropped, when all eight are in
    /// use (the original then makes no order).
    /// @orig 0x00321838 SubTactic_Alloc (unknown)
    [[nodiscard]] std::optional<std::size_t> allocate(std::unique_ptr<SubTactic> order);
    /// Frees record `slot`; nothing for a slot outside the pool or free already.
    /// @orig 0x00321890 SubTactic_Free (unknown)
    void free(std::size_t slot);
    /// The order in record `slot`; null for a free record or a slot outside the pool.
    [[nodiscard]] SubTactic* at(std::size_t slot) const;
    /// How many records are in use.
    [[nodiscard]] std::size_t inUse() const;

  private:
    std::array<std::unique_ptr<SubTactic>, kSubTacticSlots> m_records{}; // a record in use holds its order
};

/// The arguments the draw builds an order with (AttackTactic_StartSubTactic's table): the kind, the member count
/// (Charge, Leader, Throw), the flag (Guard's back-off, Throw's pile), the time limit (negative: none) and Guard's own
/// deadline, game time ms.
struct OrderArgs {
    OrderKind kind = OrderKind::Throw;
    int count = 0;
    bool flag = false;
    std::int64_t limitMs = -1;
    std::uint64_t deadlineAtMs = 0;
};

/// **Coney stand-in** for the five orders, until #338 (Charge `0x00321db8`, Leader `0x00322708`, Grab `0x00321ad8`),
/// #339 (Guard `0x00322188`) and #340 (Throw `0x00322b78`) port them: it keeps what the draw made it with and orders
/// nobody. Charge, Leader and Throw last their limit; Guard is done once past its deadline (the real one is too, after
/// its closing line); Grab is done on its first process, as the real one is when it finds nothing to grab.
class StandInOrder final : public SubTactic {
  public:
    /// The order `args` describe, for gang `gang`.
    /// @coney-only stand-in for the five orders' constructors until #338, #339 and #340 port them
    StandInOrder(int gang, const OrderArgs& args) : SubTactic(gang, args.limitMs), m_args(args) {}

    /// @coney-only stand-in: the kind the real order's vtable gives
    [[nodiscard]] OrderKind kind() const override { return m_args.kind; }
    /// What the draw made it with.
    [[nodiscard]] const OrderArgs& args() const { return m_args; }

  protected:
    /// @coney-only stand-in for the orders' processes until #338, #339 and #340 port them
    [[nodiscard]] bool onProcess(Gang& gang) override;

  private:
    OrderArgs m_args;
};

} // namespace coney::ai
