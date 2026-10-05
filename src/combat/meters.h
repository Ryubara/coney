// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/anim_ranges.h"
#include "combat/combat_tuning.h"

// A fighter's numbers: health, the damage pending this update, the power meter grabs spend and the rage meter hits
// fill. Time-based meters take the game time in whole milliseconds, as the original keeps it, and carry the fraction
// of a point between updates, so at the fixed 1/30 s step they move by the researched amounts per update.
// Research: docs/research/combat.md#damage, docs/research/combat.md#power-meter, docs/research/combat.md#rage

namespace coney::combat {

/// The player's power class values read at runtime (class 64, docs/research/characters.md#power-classes).
inline constexpr int kPlayerPowerMax = 400;            ///< `+0x28`, the power meter's maximum.
inline constexpr int kPlayerPowerRefillPerSecond = 60; ///< `+0x2a`.
/// The player's Warrior class values (class 6): the rage maximum (s16 `+0x00`) and the gain percentage (`+0x02`).
inline constexpr int kPlayerRageMax = 78;
inline constexpr int kPlayerRageGainPercent = 144;

/// A human's health (record `+0x144`) and its maximum (`+0x146`).
class Health {
  public:
    /// Full health of `maximum` (at least 1, CONEY_ASSERT).
    explicit Health(int maximum);
    /// `value` of `maximum`, `value` clamped to 0..maximum.
    Health(int value, int maximum);

    [[nodiscard]] int value() const { return m_value; }
    [[nodiscard]] int maximum() const { return m_maximum; }
    /// Health over its maximum, 0 to 1.
    /// @orig 0x00222ef0 Human_HealthPercent (unknown)
    [[nodiscard]] float fraction() const;
    /// Takes `damage` off (never below 0); returns what was taken.
    int apply(int damage);
    /// Health is 0.
    [[nodiscard]] bool depleted() const { return m_value == 0; }

  private:
    int m_value;
    int m_maximum;
};

/// The damage waiting to be applied to a human this update: the largest of the update's hits, with its kind.
class PendingDamage {
  public:
    /// Keeps `damage` and `kind` when the damage is larger than what is pending (record `+0x118`, `+0x11a`).
    /// @orig 0x00264bd8 Human_AddPendingDamage (unknown)
    void add(int damage, int kind);
    /// The pending damage (0 for none).
    [[nodiscard]] int damage() const { return m_damage; }
    /// The pending hit's kind.
    [[nodiscard]] int kind() const { return m_kind; }
    /// Applies the pending damage to `health` and clears it; returns what was taken.
    int applyTo(Health& health);

  private:
    int m_damage = 0;
    int m_kind = 0;
};

/// A hit's damage: anim `animId`'s Anim Range List damage, doubled when the attacker deals double damage (human flag
/// `0x4000`) and quartered for class `0x80` against brain type 3.
///
/// Not yet here: a held weapon's bonus and the player's upgrade percentages (Coney has neither yet).
/// @orig 0x0021b290 Strike_Contact (unknown)
[[nodiscard]] int strikeDamage(const AnimRangeList& ranges, int animId, bool doubled = false, bool quartered = false);

/// The power meter (record `+0x148`): grab strikes and throws spend it, it drains while grabbing or tackling and
/// refills otherwise.
class PowerMeter {
  public:
    /// A full meter of `maximum` refilling `refillPerSecond`, its clock starting at `startMs`.
    explicit PowerMeter(int maximum = kPlayerPowerMax, int refillPerSecond = kPlayerPowerRefillPerSecond,
                        std::uint64_t startMs = 0);

    [[nodiscard]] int value() const { return m_value; }
    [[nodiscard]] int maximum() const { return m_maximum; }
    /// The meter over its maximum, 0 to 1.
    /// @orig 0x00226510 Human_PowerFraction (unknown)
    [[nodiscard]] float fraction() const;
    /// Spends `fraction` of the maximum, rounded (never below 0); returns what was spent.
    /// @orig 0x00226448 Human_SpendPower (unknown)
    int spend(float fraction);
    /// Sets the meter (clamped to 0..maximum), as a script or a test may.
    void set(int value);
    /// The human's hurt state: while `hurt` the maximum is the class's × `factor` (`int(x + 0.5)`), the meter held
    /// to it; otherwise the class's again (the meter refills to it).
    /// @orig 0x00223068 Human_PowerMax (unknown)
    void setHurt(bool hurt, float factor);

    /// Moves the meter to game time `nowMs`: down by `drainPerSecond` while `draining` (grabbing or tackling), else up
    /// by the refill rate, to the maximum. The fraction of a point is carried to the next update.
    /// **Coney choice**: the carried fraction restarts when the meter turns from refilling to draining or back.
    void update(std::uint64_t nowMs, bool draining, float drainPerSecond);

  private:
    int m_value;
    int m_maximum;
    int m_classMaximum; // the class's maximum, before the hurt scale
    int m_refillPerSecond;
    std::uint64_t m_lastMs;
    float m_carry = 0.0F;
    bool m_draining = false;
};

/// What scales one rage gain besides the points: the per-player halving and the multiplier in state `0x1000`.
struct RageGain {
    bool halved = false;          ///< A per-player flag halves the gain.
    float stateMultiplier = 1.0F; ///< The multiplier while in state `0x1000` (its value is not researched).
};

/// The rage meter (human `+0x650`), filled by hits and spent by rage.
class RageMeter {
  public:
    /// An empty meter of `maximum` with the Warrior class's `gainPercent`, its clock starting at `startMs`.
    explicit RageMeter(int maximum = kPlayerRageMax, int gainPercent = kPlayerRageGainPercent,
                       std::uint64_t startMs = 0);

    [[nodiscard]] int value() const { return m_value; }
    [[nodiscard]] int maximum() const { return m_maximum; }
    [[nodiscard]] bool full() const { return m_value >= m_maximum; }
    [[nodiscard]] bool raging() const { return m_raging; }
    /// Sets the meter (clamped), as a script or a test may.
    void set(int value);

    /// Adds `trunc(points × f × gain / 100 × h × s)` to the maximum, where `f` is the factor below the cap for an award
    /// of up to CombatTuning::ragePointsCap points and the factor above it for a larger one (the whole award), and
    /// holds the meter for CombatTuning::rageHoldMs from `nowMs`; nothing while raging. Returns what was added. Only a
    /// player gains rage: the caller checks that.
    /// @orig 0x00264cf8 Human_AddRage (unknown)
    int add(float points, const CombatTuning& tuning, std::uint64_t nowMs, RageGain gain = {});

    /// Starts rage when the meter is full and rage is not on; returns whether it started.
    /// @orig 0x002843f8 Player_StartRage (unknown)
    bool start(std::uint64_t nowMs);

    /// Moves to game time `nowMs`: while raging the meter drains at CombatTuning::rageDrainPerSecond and rage ends when
    /// it is empty; otherwise, once the hold of the last gain has passed, it decays at
    /// CombatTuning::rageDecayPerSecond.
    /// @orig 0x002562d0 Human_DrainMeters (unknown)
    void update(std::uint64_t nowMs, const CombatTuning& tuning);

  private:
    int m_value = 0;
    int m_maximum;
    int m_gainPercent;
    std::uint64_t m_lastMs;
    std::uint64_t m_holdUntilMs = 0; // the meter does not decay before this (human `+0x648`)
    float m_carry = 0.0F;
    bool m_raging = false;
};

} // namespace coney::combat
