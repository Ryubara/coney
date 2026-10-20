// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_attack.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/brain.h"
#include "ai/brain_events.h"
#include "ai/gangs.h"
#include "ai/hub_goals.h"
#include "ai/melee_goal.h"

namespace coney::ai {

namespace {

// The threat response the tactic gives its fighters (brain `+0x21c`), and its fleeing members.
constexpr int kAttackThreatResponse = 2;
constexpr int kFleeThreatResponse = 0;
// The spacing: with no enemy listed, and the base against an enemy walking or slower, or moving faster.
constexpr std::uint16_t kNoEnemySpacing = 300;
constexpr std::uint16_t kSlowEnemySpacing = 1000;
constexpr std::uint16_t kFastEnemySpacing = 50;
// The enemy's stored gait (human `+0x1a8`) above which he counts as fast.
constexpr int kFastGait = 2;
// The member slots in order (a copy: a goal pushed may tell a tactic, which may change the members).
std::vector<Brain*> membersOf(const Gang& gang) { return gang.members(); }

// Pops `brain` to its goal base, marks it and pushes the Melee goal with no time limit (`Goal_Melee(him, −1)`).
void pushMelee(Brain& brain) { static_cast<void>(brain.pushTacticGoal(std::make_unique<MeleeGoal>(kNoFightLimit))); }

// The cower clips the gang takes (table `0x005110c8` for kind 6 with team-ups, `gen_cower_plead` for a gang that can
// flee with a flee share), none otherwise. Start and End read the same rule.
std::span<const std::string_view> cowerClips(const Gang& gang) {
    static constexpr std::array<std::string_view, 2> kTeamUp{"punk_grab_struggle", "punk_grab_struggle_react"};
    static constexpr std::array<std::string_view, 1> kFlee{"gen_cower_plead"};
    if (gang.kind() == kTeamUpCowerKind && gang.orders().attackStrategies) {
        return kTeamUp;
    }
    if (gang.canFlee() && gang.owner().fleeShare(gang.kind()) != 0) {
        return kFlee;
    }
    return {};
}

// The squared distance between two members' positions.
float squaredDistance(const Brain& a, const Brain& b) {
    const anim::Vec3 from = a.human().position();
    const anim::Vec3 to = b.human().position();
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    return (dx * dx) + (dy * dy) + (dz * dz);
}

// The enemy on `brain`'s list nearest him (`HandleArray16_NearestHandle`); null when the list is empty.
Brain* nearestListed(const Brain& brain) {
    Brain* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (Brain* enemy : brain.enemies()) {
        const float distance = squaredDistance(brain, *enemy);
        if (distance < best) {
            best = distance;
            nearest = enemy;
        }
    }
    return nearest;
}

// Whether `brain`'s human is cuffed. **Coney's reading**: the script's arrested state stands for the cuffs.
bool cuffed(const Brain& brain) { return brain.human().script().arrested; }

} // namespace

Brain* nearestGangEnemy(Brain& brain, Gangs& gangs) {
    const Gang* own = brain.gang();
    Brain* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (std::size_t id = 0; id < kGangSlots; ++id) {
        Gang* other = gangs.find(static_cast<int>(id));
        if (other == nullptr || other == own || !(Gangs::enemies(own, other) || Gangs::enemies(other, own))) {
            continue;
        }
        for (Brain* member : other->members()) {
            if (!Brain::fightable(*member)) {
                continue;
            }
            const float distance = brain.distanceTo(*member);
            if (distance < best) {
                best = distance;
                nearest = member;
            }
        }
    }
    return nearest;
}

void giveMelee(Brain& brain) {
    if (brain.findGoal(kMeleeGoal) == nullptr) {
        static_cast<void>(brain.pushGoal(std::make_unique<MeleeGoal>(kNoFightLimit)));
    }
}

TacticAttack::TacticAttack(std::string callback, std::uint64_t nowMs)
    : Tactic(kAttackTactic, std::move(callback)), m_nextEnemyCheckMs(nowMs + kAttackEnemyCheckMs),
      m_nextIdlePassMs(nowMs + kAttackIdleCheckMs) {}

void TacticAttack::start(Gang& gang) {
    // 1. The spacing, then every standing member's Melee.
    computeSpacing(gang);
    giveMelee(gang);
    // 2. The spot line is not built (stand-in). 3. The cower clips.
    if (const std::span<const std::string_view> clips = cowerClips(gang); !clips.empty()) {
        gang.setClipGroup(kCowerClips, clips);
    }
    // 4. The living count the flight compares with (`Gang_CountLiving(gang, 0)`: the dead count until deleted), noted
    // once.
    if (gang.canFlee() && gang.fleeCount() == -1) {
        gang.setFleeCount(gang.living());
    }
}

void TacticAttack::end(Gang& gang) {
    if (!cowerClips(gang).empty()) {
        gang.releaseClipGroup(kCowerClips);
    }
}

void TacticAttack::computeSpacing(Gang& gang) {
    // The first member listing an enemy, and the enemy nearest him.
    const Brain* enemy = nullptr;
    for (const Brain* member : gang.members()) {
        if (!member->enemies().empty()) {
            enemy = nearestListed(*member);
            break;
        }
    }
    if (enemy == nullptr) {
        m_spacing = kNoEnemySpacing;
        return;
    }
    m_spacingBase = static_cast<int>(enemy->human().gait()) > kFastGait ? kFastEnemySpacing : kSlowEnemySpacing;
    // Over the living count (`Gang_CountLiving(gang, 0)`), at least 1 here: the member listing the enemy is one.
    m_spacing = static_cast<std::uint16_t>(m_spacingBase / gang.living());
}

void TacticAttack::giveMelee(Gang& gang) {
    for (Brain* member : membersOf(gang)) {
        if (downOrDead(*member) || member->findGoal(GoalType::PedestrianReaction) != nullptr) {
            continue;
        }
        member->setThreatResponse(kAttackThreatResponse);
        pushMelee(*member);
    }
}

int TacticAttack::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    // 1. The enemy check: a member with an enemy listed ends the update; none reports 9. Skipped once fled.
    if (now > m_nextEnemyCheckMs) {
        m_nextEnemyCheckMs = now + kAttackEnemyCheckMs;
        if (!m_fled) {
            for (const Brain* member : gang.members()) {
                if (!member->enemies().empty()) {
                    return 0;
                }
            }
            return kTacNoEnemies;
        }
    }
    // 2. The idle pass: each member with an empty stack melees and goes over to the farthest busy mate. A member given
    // the Melee goal earlier in the same pass counts as busy for the later ones.
    if (now > m_nextIdlePassMs) {
        m_nextIdlePassMs = now + kAttackIdleCheckMs;
        const std::vector<Brain*> members = membersOf(gang);
        for (Brain* idle : members) {
            if (idle->goalCount() != 0) {
                continue;
            }
            pushMelee(*idle);
            Brain* farthest = nullptr;
            float best = 0.0F;
            for (Brain* mate : members) {
                if (mate == idle || mate->goalCount() == 0) {
                    continue;
                }
                if (const float distance = squaredDistance(*idle, *mate); distance > best) {
                    best = distance;
                    farthest = mate;
                }
            }
            if (farthest != nullptr) {
                sendHelper(*idle, *farthest);
            }
        }
    }
    // 3. The team-up timer is Unit 3's (not built).
    return 0;
}

bool TacticAttack::event(Gang& gang, Brain& member, const BrainEvent& event) {
    switch (event.id) {
    case kEventDamaged:
    case kEventEnemyAdded:
        // An own member with no fight goal (and no order running: none is built) melees.
        if (member.gang() == &gang && !member.hasFightGoal()) {
            pushMelee(member);
        }
        break;
    case kGangMessageHeadcount:
        if (!m_fled) {
            fireCallback(gang, kTacMemberDied);
        }
        break;
    case kEventDown:
        checkFlee(gang);
        break;
    case kGangMessageRegive:
    case kGangMessageMembers:
        giveMelee(gang);
        break;
    case kEventViolence:
        onViolence(gang, event);
        break;
    default:
        break;
    }
    return false;
}

void TacticAttack::onViolence(Gang& gang, const BrainEvent& event) {
    // The leader takes the side for the gang.
    Brain* leader = gang.leader();
    if (leader == nullptr) {
        return;
    }
    Brain* picked = pickSideInFight(*leader, event);
    if (picked == nullptr) {
        return;
    }
    for (Brain* member : membersOf(gang)) {
        if (downOrDead(*member) || member->goalCount() != 0) {
            continue;
        }
        pushMelee(*member);
        member->addEnemy(*picked);
        {
            const TargetCauseScope cause(*member, TargetCause::Help); // sent to a fight in view: a help response
            member->setTarget(picked);
        }
        if (member->findGoalAboveBase(GoalType::HelpRespond) == nullptr) {
            sendHelper(*member, *picked);
        }
    }
}

void TacticAttack::checkFlee(Gang& gang) {
    // 1. Only a gang that may flee and noted its count, with a flee share; never kind 6 with team-ups.
    const int count = gang.fleeCount();
    const int share = gang.owner().fleeShare(gang.kind());
    if (!gang.canFlee() || count <= 0 || share == 0 ||
        (gang.kind() == kTeamUpCowerKind && gang.orders().attackStrategies)) {
        return;
    }
    // 2. Beaten: the standing, uncuffed members no more than the kind's share of the count. A share that rounds to 0
    // never flees.
    const auto limit = std::lround(static_cast<double>(share) * count / 100.0);
    if (limit == 0) {
        return;
    }
    int standing = 0;
    for (const Brain* member : gang.members()) {
        if (!downOrDead(*member) && !cuffed(*member)) {
            ++standing;
        }
    }
    if (standing > limit) {
        return;
    }
    // 3. Each standing, uncuffed member of the fleeing role not already running (a PedReaction, kept on a rerun) stops
    // fighting and runs from his nearest enemy (from where he stands with none).
    bool sent = false;
    for (Brain* member : membersOf(gang)) {
        if (downOrDead(*member) || cuffed(*member) || gang.owner().roleOf(*member) != std::optional<int>(kFleeRole) ||
            member->findGoal(GoalType::PedestrianReaction) != nullptr) {
            continue;
        }
        const Brain* enemy = nearestListed(*member);
        const anim::Vec3 from = enemy != nullptr ? enemy->human().position() : member->human().position();
        member->setThreatResponse(kFleeThreatResponse);
        pushMelee(*member);
        static_cast<void>(member->pushGoal(std::make_unique<PedestrianReactionGoal>(from)));
        sent = true;
    }
    // 4. Fled only once someone ran: a gang that sent nobody keeps reporting 9 when its enemies are gone.
    if (sent) {
        m_fled = true;
    }
}

void tacticAttack(Gangs& gangs, int gang, std::string callback) {
    if (gangs.find(gang) == nullptr) {
        return;
    }
    gangs.setTactic(gang, std::make_unique<TacticAttack>(std::move(callback), gangs.nowMs()));
}

} // namespace coney::ai
