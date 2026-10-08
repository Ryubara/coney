// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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
/// His walk home and away is a run (gait 4); the wary goal is SpectateArgs::dealerWary().
inline constexpr int kDealerGait = 4;

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
    NoCash,      ///< Less money than the price: the offer is withdrawn.
    AtLimit,     ///< Already carrying the most: the offer is withdrawn.
    RippedOff,   ///< A dirty dealer took the price and gave nothing; he leaves.
    Sold,        ///< The price taken and the item given.
    PairStarted, ///< The money pair (`money_take` / `money_give`) started; the sale completes at its event.
    NotDealing   ///< He is not offering a deal.
};

/// The reach of a dealer's offer (context record kind 4, `CfgActionDistance`), m in plan.
inline constexpr float kDealReach = 1.75F;
/// The most money a dealer holds.
inline constexpr int kDealerMostMoney = 999;
/// The dealer's speech commands (docs/research/ai.md#dealer) and the least time between two `cash` lines, ms.
inline constexpr std::uint32_t kDealGreetLine = 94;
inline constexpr std::uint32_t kDealCashLine = 96;
inline constexpr std::uint32_t kDealOfferLine = 95;
inline constexpr std::uint32_t kDealThanksLine = 98;
inline constexpr std::uint32_t kDealGoodbyeLine = 99;
inline constexpr std::uint32_t kDealWaryLine = 100;
inline constexpr std::uint32_t kDealNoCashLine = 97;
inline constexpr std::uint32_t kDealLimitLine = 101;
inline constexpr std::uint32_t kDealRipOffLine = 105;
inline constexpr std::uint64_t kDealCashLineMs = 5000;

/// The gesture anim ids: 603 `ANIM_FIDGET_FIGHT` and 668 `ANIM_SPECIAL_ACTION`, and the gang kind whose clip table
/// fills them with the dealer's clips (`GangCreate(24, "FDealer")`, docs/research/ai.md#dealer-gestures).
inline constexpr int kDealerFidgetAnim = 603;
inline constexpr int kDealerActionAnim = 668;
inline constexpr int kDealerGangKind = 24;
/// A gesture's blend in and out, seconds.
inline constexpr float kDealerGestureFade = 0.5F;
/// The money pair's clips: the dealer's (668 entry 2) and the buyer's (603 entry 1, bound to his 668).
inline constexpr std::string_view kMoneyTakeClip = "money_take";
inline constexpr std::string_view kMoneyGiveClip = "money_give";

/// The clip of entry `variant` of anim `animId`'s group in the dealer gang's clip table (`GangClips_Set` from
/// `0x0050cbc0` and `0x0050cbe0`): 603 entries 0-7, 668 entries 0-2; empty for any other.
/// @orig 0x00163fc0 GangClips_Get (unknown)
[[nodiscard]] std::string_view dealerGangClip(int animId, int variant);

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

    /// Threat response 0, the type's spinning icon over his head (world_objects::dealerIcon()) and the home spot (where
    /// he stands).
    /// @orig 0x002c6e78 DealerGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Threat response back to 2, his icon removed; he no longer deals.
    /// @orig 0x002c70a0 DealerGoal_End (unknown)
    void end(Brain& brain) override;
    /// One update (docs/research/ai.md#dealer-process): the player leaving his range after a deal brings the leaving
    /// gesture and line (`thanks` after a sale, `goodbye` without one unless refused at the limit) and the reset to
    /// state 1; beyond twice the range he is forgotten. In range, nothing more while actions are queued; states 4 and
    /// 5 stay put (**Coney choice**: the run to a flag of kind 8 is not built); every 2 s while his threat response is
    /// 0 an enemy gang's member within 16 m makes him wary (a spectate goal, a turn to the player, the wary gesture and
    /// line); more than 1 m from home he walks back; more than 15° off the player he turns (before he has greeted him,
    /// or when the player stands within 2 m); in state 1 with the player within 1.5 m the offer line and state 3; the
    /// first time, a turn, the greeting gesture and line, and his radar blip when the goal was made with `option`
    /// (DealerGoal_AddRadarIcon). Every update that gets that far offers the deal. Never done. **Coney choices**: no
    /// line of sight is tested, and the run chance is kept, not read.
    /// @orig 0x002c7fd8 DealerGoal_Process (unknown)
    /// @orig 0x002c7de8 DealerGoal_Reset (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// Whether he offers a deal (human `+0x1b2` = 1): the buyer's triangle within kDealReach reaches deal().
    [[nodiscard]] bool offering() const { return m_offering; }
    /// Whether his offer's prompt (context record kind 4, GSTRING.HUD DealTerms::prompt) is registered: the last update
    /// reached its end (greeted, idle, the player in range) and no refusal withdrew it since. The HUD shows it to a
    /// player within kDealReach.
    [[nodiscard]] bool prompting() const { return m_prompting; }
    /// A buyer's triangle at the offer (event 0, `DealerBrain_OnEvent`), the buyer holding `money` dollars and
    /// `carried` of the item, which the inventory holds to `itemLimit`. The dealer turns to `buyer` and deals (state
    /// 3); then the first that applies: too little money or carrying the most (the refusal gesture, the offer
    /// withdrawn); dirty (he takes the price and leaves; **Coney choice**: the shove 21 is not queued); the money pair
    /// when both its clips are loaded and it is not playing (`money_take` on him and `money_give` on the buyer, both
    /// as anim 668: PairStarted, the sale waits for finishPair()); else the sale now (his money up by the price, to at
    /// most kDealerMostMoney, and his sales counted). The caller moves the buyer's money and item.
    /// @orig 0x002c74d8 DealerGoal_Deal (unknown)
    [[nodiscard]] DealOutcome deal(Brain& brain, Brain& buyer, int money, int carried, int itemLimit);
    /// The dealer's clip in the pair (`money_take`, his 668) passed its action event (`0x41`, 17 updates in), which
    /// `Human_HandleMessage` turns into `DealerGoal_FinishPair`: the pending sale completes (Sold, as deal()'s), once.
    /// Nothing when no pair is pending.
    /// @orig 0x002c7c20 DealerGoal_FinishPair (unknown)
    [[nodiscard]] std::optional<DealOutcome> finishPair(Brain& brain);
    /// Whether a money pair waits for its event.
    [[nodiscard]] bool pairPending() const { return m_pairPending; }
    /// The deals made (`+0x38`) and whether one was (`+0x3f`).
    /// The speech command the dealer says for a deal's `outcome` at `nowMs`: 97 `nocash`, 101 `limit`, 105 `ripoff`,
    /// and 96 `cash` for a sale at most every 5 s; nothing otherwise.
    [[nodiscard]] std::optional<std::uint32_t> dealLine(DealOutcome outcome, std::uint64_t nowMs);
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
    bool m_prompting = false;                   // the kind-4 prompt registered by this update (steps 6, 8)
    std::optional<std::uint64_t> m_lastCashMs;  // when he last said `cash`
    bool m_pairPending = false;                 // +0x45: the money pair started by a deal

    // Queues gesture `animId` (603 or 668) of `variant` (`DealerGoal_QueueGesture`): the dealer gang's clip for it,
    // or, for another gang, his anim set's own.
    // @orig 0x002c7158 DealerGoal_QueueGesture (unknown)
    void gesture(Brain& brain, int animId, int variant);
    // Completes a sale: his money up by the price, the visit's sale and the count.
    void completeSale(Brain& brain);
};

} // namespace coney::ai
