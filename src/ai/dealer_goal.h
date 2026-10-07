// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "ai/goal.h"
#include "animation/anim_math.h"

// GoalDealer: a street dealer who stands at his spot, never fights while dealing, turns to the player, greets him the
// first time he comes in range and deals once he stands close; an enemy close by makes him wary for a while. Leaving
// after a deal sends him off (states 4 and 5).
// Research: docs/research/ai.md#dealer

namespace coney::ai {

class ScriptServices;

/// The dealer's states (`+0x24`): 1 waiting (the start), 3 dealing, 4 and 5 leaving after a deal.
enum class DealerState : std::uint8_t {
    Waiting = 1,
    Dealing = 3,
    Leaving = 4,
    LeavingOption = 5,
};

/// The dealer's numbers: he deals within this of the player (m, 1.5 m squared 2.25), walks home beyond this from his
/// spot (m), turns beyond this off the player (radians, 15°), looks for enemies this often (ms) within this (m), and a
/// greeted dealer turns to a standing player within this (m, 4 squared).
inline constexpr float kDealDistance = 1.5F;
inline constexpr float kHomeDistance = 1.0F;
inline constexpr float kDealerTurnAngle = 0.2617994F;
inline constexpr std::uint64_t kWaryPeriodMs = 2000;
inline constexpr float kWaryDistance = 16.0F;
inline constexpr float kGreetedTurnDistance = 2.0F;
/// His walk home and away is a run (gait 4); the wary goal lasts 2-4 s.
inline constexpr int kDealerGait = 4;
inline constexpr int kWaryMinMs = 2000;
inline constexpr int kWaryMaxMs = 4000;

/// One dealer type's deal (the table at `0x005110f8`, 8 bytes a type): the `GSTRING.HUD` prompt, the item sold, its
/// price in dollars, the most a buyer may carry (0 for the item's own limit: the flash's 3, or 4 with the revive
/// upgrade, is the inventory's) and how many one deal gives.
struct DealTerms {
    int prompt = 0;
    int item = 0;
    int price = 0;
    int mostCarried = 0;
    int amount = 0;
};

/// The terms of a dealer of `type` (dealerTypeFor()): 0 the flash, 1 weapons, 2 spray paint; nothing for another.
[[nodiscard]] std::optional<DealTerms> dealTerms(int type);

/// How a buyer's triangle at a dealing dealer ends (docs/research/ai.md#dealer).
enum class DealOutcome : std::uint8_t {
    NoCash,    ///< Less money than the price: the offer is withdrawn.
    AtLimit,   ///< Already carrying the most: the offer is withdrawn.
    RippedOff, ///< A dirty dealer took the price and gave nothing; he leaves.
    Sold,      ///< The price taken and the item given.
    NotDealing ///< He is not offering a deal.
};

/// The reach of a dealer's offer (context record kind 4, `CfgActionDistance`), m in plan.
inline constexpr float kDealReach = 1.75F;
/// The most money a dealer holds.
inline constexpr int kDealerMostMoney = 999;

/// The kind of goods a dealer sells, from `GoalDealer`'s type or his class: 0 flash, 1 weapons, 2 the third kind.
/// Classes 426-430 sell flash, 431-435 the third kind and 436-440 weapons, whatever the type says.
/// @orig 0x002c6c88 Goal_Dealer (unknown)
[[nodiscard]] int dealerTypeFor(int characterClass, int type);

/// The dealer goal (type `0x80`, vtable `0x00540c90`).
/// @orig 0x002c6d90 DealerGoal_Init (unknown)
class DealerGoal final : public Goal {
  public:
    /// A dealer of `type` (dealerTypeFor()) dealing within `range` m, with `runChance` and `dirtyChance` percent and
    /// `option` (a radar icon when greeting), finding the player through `services` (which must outlive it). Whether
    /// he is dirty is rolled when the goal starts (**Coney choice**: the original rolls in its constructor, which has
    /// no generator in Coney's).
    DealerGoal(ScriptServices& services, int type, float range, int runChance, int dirtyChance, bool option)
        : Goal(GoalType::Dealer), m_services(&services), m_type(type), m_range(range), m_runChance(runChance),
          m_dirtyChance(dirtyChance), m_option(option) {}

    /// Threat response 0 and the home spot (where he stands). **Coney choice**: the spinning icon is not built.
    /// @orig 0x002c6e78 DealerGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Threat response back to 2; he no longer deals.
    /// @orig 0x002c70a0 DealerGoal_End (unknown)
    void end(Brain& brain) override;
    /// One update: the player in range or not (leaving after a deal: state 4, or 5 with the option, and a turn to
    /// him; beyond twice the range he is forgotten); waits while actions are queued; states 4 and 5 stay put
    /// (**Coney choice**: the run to a flag of kind 8 is not built); every 2 s while his threat response is 0 an enemy
    /// gang's member within 16 m makes him wary (a spectate goal of 2-4 s and a turn to it); more than 1 m
    /// from home he walks back; more than 15° off the player he turns (before he has greeted him, or when the player
    /// stands within 2 m); the first time, he greets him; in state 1 (as constructed) with the player within 1.5 m
    /// he deals (state 3). Never done. **Coney choices**: no line of sight is tested, the gestures, the buy clip
    /// in the player's slot and the radar icon are not built, and the run and dirty chances are kept, not read.
    /// @orig 0x002c7fd8 DealerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// Whether he offers a deal (state 3, human `+0x1b2` = 1): the buyer's triangle within kDealReach reaches deal().
    [[nodiscard]] bool offering() const { return m_offering && m_state == DealerState::Dealing; }
    /// A buyer's triangle at the offer (event 0, `DealerBrain_OnEvent`), the buyer holding `money` dollars and
    /// `carried` of the item, which the inventory holds to `itemLimit`. The dealer turns to `buyer`; then, as the
    /// outcome says, the offer is withdrawn (no cash, at the limit), he takes the price and leaves (dirty), or he
    /// sells: his money rises by the price (to at most kDealerMostMoney) and his sales count. The caller moves the
    /// buyer's money and item. **Coney choices**: the dealer's speech and gestures and the pair `money_take.anm` /
    /// `money_give.anm` are not played (the original completes the deal at once when the pair cannot load), and a
    /// dirty dealer queues no shove.
    /// @orig 0x002c74d8 DealerGoal_Deal (unknown)
    [[nodiscard]] DealOutcome deal(Brain& brain, const Brain& buyer, int money, int carried, int itemLimit);
    /// The deals made (`+0x38`) and whether one was (`+0x3f`).
    [[nodiscard]] int sales() const { return m_sales; }
    [[nodiscard]] bool sold() const { return m_sold; }

    /// His state, kind of goods, whether he is dirty, has greeted the player and is dealing, and whether the player is
    /// in range.
    [[nodiscard]] DealerState state() const { return m_state; }
    [[nodiscard]] int type() const { return m_type; }
    [[nodiscard]] bool dirty() const { return m_dirty; }
    [[nodiscard]] bool greeted() const { return m_greeted; }
    [[nodiscard]] bool dealing() const { return m_dealing; }
    [[nodiscard]] bool playerInRange() const { return m_playerInRange; }
    /// His spot.
    [[nodiscard]] anim::Vec3 home() const { return m_home; }

  private:
    ScriptServices* m_services;
    int m_type;                                 // +0x28
    float m_range;                              // +0x2c
    int m_runChance;                            // +0x3a
    int m_dirtyChance;                          // +0x3b
    bool m_option;                              // +0x3c
    DealerState m_state = DealerState::Waiting; // +0x24
    anim::Vec3 m_home;                          // +0x10
    std::uint64_t m_nextScanMs = 0;             // +0x30
    bool m_greeted = false;                     // +0x3d
    bool m_playerInRange = false;               // +0x3e
    bool m_dirty = false;                       // +0x41
    bool m_dealing = false;                     // +0x42
    int m_sales = 0;                            // +0x38
    bool m_sold = false;                        // +0x3f
    bool m_atLimit = false;                     // +0x40
    bool m_offering = false;                    // the dealer human's +0x1b2
};

} // namespace coney::ai
