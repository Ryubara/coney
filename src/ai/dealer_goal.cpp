// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/dealer_goal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/idle_goals.h"
#include "ai/move_action.h"
#include "ai/play_anim_action.h"
#include "ai/script_services.h"
#include "ai/turn_action.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "warriors/inventory.h"
#include "world_objects/spinning_icons.h"

namespace coney::ai {

namespace {

// A dealer type's radar blip (docs/research/ai.md#dealer-icon): its blip type and icon, both at 0.8.
// @orig 0x002c7ee0 DealerGoal_AddRadarIcon (unknown)
struct DealerBlip {
    int type;
    int icon;
};
constexpr std::array<DealerBlip, 3> kDealerBlips{{{2, 29}, {4, 31}, {3, 30}}};
constexpr float kDealerBlipFactor = 0.8F;

// The dealer classes and the kinds they sell.
constexpr int kFlashClassFirst = 426;
constexpr int kThirdClassFirst = 431;
constexpr int kWeaponClassFirst = 436;
constexpr int kDealerClassLast = 440;
constexpr int kFlashType = 0;
constexpr int kWeaponType = 1;
constexpr int kThirdType = 2;
// The wary gesture's entry of 603 (`dlr_nearfight`).
constexpr int kWaryVariant = 7;

// The horizontal distance between two points.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// How far `brain`'s human is off facing `point`, radians.
float offFacing(const Brain& brain, anim::Vec3 point) {
    const anim::Vec3 way = anim::subtract(point, brain.human().position());
    if (std::hypot(way.x, way.y) <= 1e-4F) {
        return 0.0F;
    }
    return std::fabs(human::wrapAngle(human::headingOf(way) - brain.human().heading()));
}

// The nearest member of a gang the dealer's gang has as an enemy, and its distance; null when none.
const Brain* nearestEnemy(const Brain& dealer, float& distance) {
    const Gang* own = dealer.gang();
    if (own == nullptr) {
        return nullptr;
    }
    const Brain* nearest = nullptr;
    for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
        const Gang* other = own->owner().find(id);
        if (other == nullptr || !Gangs::enemies(own, other)) {
            continue;
        }
        for (const Brain* member : other->members()) {
            if (const float d = dealer.distanceTo(*member); nearest == nullptr || d < distance) {
                nearest = member;
                distance = d;
            }
        }
    }
    return nearest;
}

} // namespace

std::optional<DealTerms> dealTerms(int type) {
    // The table at `0x005110f8`: {prompt, item, price, most carried, amount}; the flash's most carried is the
    // inventory's revive limit.
    switch (type) {
    case kFlashType:
        return DealTerms{.prompt = 6, .item = item::kRevive, .price = 20, .mostCarried = 0, .amount = 1};
    case kWeaponType:
        return DealTerms{.prompt = 7, .item = 4, .price = 50, .mostCarried = 8, .amount = 1};
    case kThirdType:
        return DealTerms{.prompt = 5, .item = item::kSprayPaint, .price = 5, .mostCarried = 9, .amount = 1};
    default:
        return std::nullopt;
    }
}

int dealerTypeFor(int characterClass, int type) {
    if (characterClass >= kFlashClassFirst && characterClass < kThirdClassFirst) {
        return kFlashType;
    }
    if (characterClass >= kThirdClassFirst && characterClass < kWeaponClassFirst) {
        return kThirdType;
    }
    if (characterClass >= kWeaponClassFirst && characterClass <= kDealerClassLast) {
        return kWeaponType;
    }
    return type;
}

void DealerGoal::start(Brain& brain) {
    brain.setThreatResponse(0);
    if (const std::string_view icon = world_objects::dealerIcon(m_type); !icon.empty()) {
        brain.human().script().icon = std::string(icon);
        brain.human().script().iconParam = 0;
    }
    m_home = brain.human().position();
    m_dirty = rollRange(brain.random(), 0, 99) < m_dirtyChance;
}

void DealerGoal::end(Brain& brain) {
    brain.setThreatResponse(kDefaultThreatResponse);
    // His icon goes with the goal (a script's own icon given since is left alone).
    if (brain.human().script().icon == world_objects::dealerIcon(m_type)) {
        brain.human().script().icon.clear();
    }
    m_dealing = false;
    m_offering = false;
    m_prompting = false;
}

GoalStatus DealerGoal::process(Brain& brain) {
    // The kind-4 prompt is registered anew by each update that reaches the end (step 11); an earlier return leaves it
    // withdrawn.
    m_prompting = false;
    // 1. The player in range, or leaving it.
    Brain* player = m_services->player();
    const float distance = player != nullptr ? brain.distanceTo(*player) : std::numeric_limits<float>::max();
    const bool wasInRange = m_playerInRange;
    m_playerInRange = player != nullptr && distance <= m_range;
    if (!m_playerInRange) {
        if (wasInRange && m_dealing && player != nullptr) {
            // The leaving gesture and line: thanks after a sale, goodbye without one (not after "limit").
            if (brain.actionCount() == 0) {
                brain.queueAction(TurnAction::toPoint(player->human().position()));
            }
            if (m_sold) {
                gesture(brain, kDealerFidgetAnim, rollRange(brain.random(), 5, 6));
                m_services->say(brain, static_cast<int>(kDealThanksLine), true, player->handle());
            } else if (!m_atLimit) {
                gesture(brain, kDealerFidgetAnim, 4);
                m_services->say(brain, static_cast<int>(kDealGoodbyeLine), true, player->handle());
            }
        }
        if (wasInRange) {
            // The reset: a new visit starts in state 1.
            m_sold = false;
            m_atLimit = false;
            m_dealing = false;
            m_pairPending = false;
            m_offering = false;
            if (m_state != DealerState::Leaving && m_state != DealerState::LeavingOption) {
                m_state = DealerState::Waiting;
            }
            m_nextScanMs = 0;
        }
        if (distance > m_range + m_range) {
            m_greeted = false;
        }
        return GoalStatus::Stop;
    }
    // 2. Actions queued (a gesture, a turn) or leaving: nothing more, and no offer while busy.
    if (brain.actionCount() > 0 || m_state == DealerState::Leaving || m_state == DealerState::LeavingOption) {
        m_offering = false;
        return GoalStatus::Stop;
    }
    const anim::Vec3 at = player->human().position();
    // 3. Every 2 s while he would not fight: an enemy close to him makes him wary.
    if (m_nextScanMs < brain.nowMs()) {
        m_nextScanMs = brain.nowMs() + kWaryPeriodMs;
        float enemyDistance = 0.0F;
        if (brain.threatResponse() == 0) {
            const Brain* enemy = nearestEnemy(brain, enemyDistance);
            if (enemy != nullptr && enemyDistance < kWaryDistance) {
                brain.pushGoal(std::make_unique<SpectateGoal>(SpectateArgs::dealerWary()));
                brain.queueAction(TurnAction::toPoint(at));
                gesture(brain, kDealerFidgetAnim, kWaryVariant);
                m_services->say(brain, static_cast<int>(kDealWaryLine), true, player->handle());
                return GoalStatus::Stop;
            }
        }
    }
    // 4. Back to his spot.
    if (planDistance(brain.human().position(), m_home) > kHomeDistance) {
        brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = m_home,
                                                                   .radius = kHomeDistance,
                                                                   .gait = kDealerGait,
                                                                   .option = false,
                                                                   .delayMs = kRandomDelay,
                                                                   .flagKind = false}));
        return GoalStatus::Stop;
    }
    // 5. Facing the player: before greeting him, or when he stands close.
    if (offFacing(brain, at) > kDealerTurnAngle) {
        const bool playerStill = player->human().gait() == human::Gait::Standing;
        if (!m_greeted || (playerStill && distance < kGreetedTurnDistance)) {
            brain.queueAction(TurnAction::toPoint(at));
            return GoalStatus::Stop;
        }
    }
    // 6. The offer once the player is close (state 1 to 3).
    if (m_state == DealerState::Waiting && distance < kDealDistance) {
        m_services->say(brain, static_cast<int>(kDealOfferLine), true, player->handle());
        m_state = DealerState::Dealing;
        m_dealing = true;
        m_offering = true;
        m_prompting = true;
        return GoalStatus::Stop;
    }
    // 7. The greeting, the first time: a turn, the gesture, the line and the radar blip.
    if (!m_greeted) {
        m_greeted = true;
        brain.queueAction(TurnAction::toPoint(at));
        gesture(brain, kDealerActionAnim, rollRange(brain.random(), 0, 1));
        m_services->say(brain, static_cast<int>(kDealGreetLine), true, player->handle());
        // The blip: type 2, 4 or 3 with icon 29, 31 or 30 at 0.8 for dealer types 0, 1, 2.
        if (m_option && m_type >= 0 && m_type < static_cast<int>(kDealerBlips.size())) {
            const DealerBlip& blip = kDealerBlips.at(static_cast<std::size_t>(m_type));
            m_services->addRadarIcon(brain, blip.type, blip.icon, kDealerBlipFactor);
        }
    }
    // 8. From the greeting on, while he is idle and the player in range: the deal offered and its prompt (kind 4)
    // registered, until the next update.
    m_offering = true;
    m_prompting = true;
    return GoalStatus::Stop;
}

DealOutcome DealerGoal::deal(Brain& brain, Brain& buyer, int money, int carried, int itemLimit) {
    const std::optional<DealTerms> terms = dealTerms(m_type);
    if (!offering() || !terms) {
        return DealOutcome::NotDealing;
    }
    // 1. He turns to the buyer and deals.
    brain.queueAction(TurnAction::toPoint(buyer.human().position()));
    m_atLimit = false;
    m_dealing = true;
    if (m_state == DealerState::Waiting) {
        m_state = DealerState::Dealing;
    }
    // 2. Too little money, or carrying the most already: the refusal and the offer withdrawn.
    if (money < terms->price) {
        gesture(brain, kDealerFidgetAnim, rollRange(brain.random(), 2, 3));
        m_offering = false;
        m_prompting = false;
        return DealOutcome::NoCash;
    }
    const int most = terms->mostCarried > 0 ? std::min(terms->mostCarried, itemLimit) : itemLimit;
    if (carried >= most) {
        gesture(brain, kDealerFidgetAnim, rollRange(brain.random(), 2, 3));
        m_atLimit = true;
        m_offering = false;
        m_prompting = false;
        return DealOutcome::AtLimit;
    }
    // 3. A dirty dealer keeps the price and runs.
    if (m_dirty) {
        int& takings = brain.human().script().money;
        takings = std::min(kDealerMostMoney, takings + terms->price);
        m_state = DealerState::Leaving;
        m_offering = false;
        m_prompting = false;
        return DealOutcome::RippedOff;
    }
    // 4. The money pair, when both clips are loaded and it is not playing: the sale waits for its event.
    const bool pairPlaying = brain.human().animator().animId() == static_cast<std::uint32_t>(kDealerActionAnim);
    if (!pairPlaying && m_services->clipAvailable(kMoneyTakeClip) && m_services->clipAvailable(kMoneyGiveClip)) {
        brain.clearActions();
        static_cast<void>(m_services->playNamedClip(brain, kDealerActionAnim, kMoneyTakeClip, kDealerGestureFade));
        static_cast<void>(m_services->playNamedClip(buyer, kDealerActionAnim, kMoneyGiveClip, kDealerGestureFade));
        m_pairPending = true;
        return DealOutcome::PairStarted;
    }
    // 5. The sale now.
    completeSale(brain);
    return DealOutcome::Sold;
}

std::optional<DealOutcome> DealerGoal::finishPair(Brain& brain) {
    if (!m_pairPending) {
        return std::nullopt;
    }
    completeSale(brain);
    return DealOutcome::Sold;
}

void DealerGoal::completeSale(Brain& brain) {
    m_pairPending = false;
    if (const std::optional<DealTerms> terms = dealTerms(m_type)) {
        int& takings = brain.human().script().money;
        takings = std::min(kDealerMostMoney, takings + terms->price);
    }
    m_sold = true;
    ++m_sales;
}

void DealerGoal::gesture(Brain& brain, int animId, int variant) {
    const Gang* gang = brain.gang();
    const std::string_view clip =
        gang != nullptr && gang->kind() == kDealerGangKind ? dealerGangClip(animId, variant) : std::string_view{};
    if (clip.empty()) {
        brain.queueAction(std::make_unique<PlayAnimAction>(*m_services, animId, false));
        return;
    }
    brain.queueAction(std::make_unique<PlayAnimAction>(*m_services, animId, std::string(clip), kDealerGestureFade));
}

std::string_view dealerGangClip(int animId, int variant) {
    // 603: the offer, the buyer's money, two refusals, goodbye, two thanks, the wary look; 668: two beckons, the
    // dealer's money.
    static constexpr std::array<std::string_view, 8> kFidget{"dlr_offer_flash", "money_give",   "dlr_refuse_1",
                                                             "dlr_refuse_2",    "dlr_goodbye1", "dlr_thanks_1",
                                                             "dlr_thanks_2",    "dlr_nearfight"};
    static constexpr std::array<std::string_view, 3> kAction{"dlr_becken_1", "dlr_becken_2", "money_take"};
    if (variant < 0) {
        return {};
    }
    const auto index = static_cast<std::size_t>(variant);
    if (animId == kDealerFidgetAnim && index < kFidget.size()) {
        return kFidget.at(index);
    }
    if (animId == kDealerActionAnim && index < kAction.size()) {
        return kAction.at(index);
    }
    return {};
}

std::optional<std::uint32_t> DealerGoal::dealLine(DealOutcome outcome, std::uint64_t nowMs) {
    switch (outcome) {
    case DealOutcome::NoCash:
        return kDealNoCashLine;
    case DealOutcome::AtLimit:
        return kDealLimitLine;
    case DealOutcome::RippedOff:
        return kDealRipOffLine;
    case DealOutcome::Sold:
        // `cash` at most every 5 s.
        if (m_lastCashMs && nowMs < *m_lastCashMs + kDealCashLineMs) {
            return std::nullopt;
        }
        m_lastCashMs = nowMs;
        return kDealCashLine;
    case DealOutcome::PairStarted:
    case DealOutcome::NotDealing:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace coney::ai
