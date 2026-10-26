// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/enemy_scan.h"

#include <algorithm>
#include <vector>

#include "ai/gangs.h"
#include "ai/perception.h"
#include "ai/script_services.h"
#include "ai/targeting.h"
#include "ai/threats.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The Warriors' filter (`Filter_IsThreatTo`): a cop only within 10 m and a threat; a civilian (type 4) not at all; any
// other when he is a threat (isThreat()) and a valid enemy (validEnemy(), which scanSees() tests right after it). The
// civilian who would pass (ped type 3 with `+0x21c` set, not held from the rear) is not modelled, the `civl_co_di`
// kind (type 6), which the original filters as any other, is refused, and a cop at exactly 10 m passes (the original
// needs under 10 m): each a **Coney stand-in**.
// @orig 0x0029bf98 Filter_IsThreatTo (unknown)
bool warriorThreat(const Brain& scanner, const Brain& member) {
    if (member.type() == BrainType::Cop) {
        return scanner.distanceTo(member) <= kScanWarriorCopRange && isThreat(scanner, member);
    }
    return member.type() != BrainType::Civilian && member.type() != BrainType::CivilianDi && isThreat(scanner, member);
}

// The near radius for `member` as `scanner` sees him: 1.5 m walking or standing, 5.2 m for a cop and a running member,
// else 3 m.
// @orig 0x0028b358 Brain_ScanEnemies (unknown)
float nearRadius(const Brain& scanner, const Brain& member) {
    const human::Gait gait = member.human().gait();
    if (scanner.type() == BrainType::Cop && gait >= human::Gait::Run) {
        return kScanNearCopRunning;
    }
    return gait < human::Gait::Jog ? kScanNearWalking : kScanNearRadius;
}

// Whether `brain`'s human is down or out: its list is emptied.
// @orig 0x0028b358 Brain_ScanEnemies (unknown)
bool down(const Brain& brain) {
    const human::Human& human = brain.human();
    return !human.alive() || human.script().knockedOut || human.outOfWorld();
}

// Whether `member` is in a gang `scanner`'s gang has as an enemy, both in use.
// @orig 0x0028b358 Brain_ScanEnemies (unknown)
bool enemyGang(const Brain& scanner, const Brain& member) {
    const Gang* mine = scanner.gang();
    const Gang* theirs = member.gang();
    return mine != nullptr && theirs != nullptr && theirs->inUse() && Gangs::enemies(mine, theirs);
}

} // namespace

// @orig 0x0028b358 Brain_ScanEnemies (unknown)
bool scanSees(const Brain& scanner, const Brain& member) {
    const human::Human& eye = scanner.human();
    const human::Human& them = member.human();
    if (&scanner == &member || scanner.distanceTo(member) > sightRangeOf(scanner)) {
        return false;
    }
    // 1. Hidden in shadow: beyond 2 m never, within it only from shadow ground.
    if (!shadowAllowsSight(eye, them)) {
        return false;
    }
    // 2. An AI scanner (not a Warrior) lists only a member less than 1.9 m above him.
    if (scanner.type() != BrainType::Player && scanner.type() != BrainType::Warrior &&
        them.position().z - eye.position().z >= kScanHeightLimit) {
        return false;
    }
    // 3. The filter.
    if (scanner.type() == BrainType::Warrior && !warriorThreat(scanner, member)) {
        return false;
    }
    if (!validEnemy(scanner, member)) {
        return false;
    }
    // 4. Sight: the near radius all round, unless he hides off shadow ground (the grace); else the cone too.
    const bool sneaking = them.hidden() && !them.onShadowGround();
    if (scanner.distanceTo(member) > nearRadius(scanner, member) || sneaking) {
        if (!inFieldOfView(scanner.fieldOfView(), eye, them.position())) {
            return false;
        }
    }
    return lineOfSight(scanner.collision(), eye.position(), them.position()).clear;
}

int scanEnemies(const Gangs& gangs, Brain& scanner) {
    // Down or out: the list goes.
    if (down(scanner)) {
        const std::vector<Brain*> old = scanner.enemies();
        for (const Brain* enemy : old) {
            scanner.forget(*enemy);
        }
        return 0;
    }
    // 1. The enemy gangs' members it sees, gang by gang.
    std::vector<Brain*> found;
    for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
        const Gang* gang = gangs.find(id);
        if (gang == nullptr) {
            continue;
        }
        for (Brain* member : gang->members()) {
            if (enemyGang(scanner, *member) && scanSees(scanner, *member)) {
                found.push_back(member);
            }
        }
    }
    // 2. The gang's shared target, when valid.
    if (const Gang* own = scanner.gang(); own != nullptr && own->chosenTarget() != nullptr) {
        Brain* shared = own->chosenTarget();
        if (shared != &scanner && validEnemy(scanner, *shared) && std::ranges::find(found, shared) == found.end()) {
            found.push_back(shared);
        }
    }
    // 3. Old entries not found again: kept while in range, valid, and near or in sight; the rest dropped.
    const std::vector<Brain*> old = scanner.enemies();
    for (Brain* enemy : old) {
        if (std::ranges::find(found, enemy) != found.end()) {
            continue;
        }
        const bool keep = scanner.distanceTo(*enemy) <= sightRangeOf(scanner) && validEnemy(scanner, *enemy) &&
                          (scanner.distanceTo(*enemy) <= kScanNearRadius || scanner.hasLineOfSight(*enemy));
        if (keep) {
            found.push_back(enemy);
        } else {
            scanner.forget(*enemy);
        }
    }
    // 4. The nearest 16; the ones beyond leave the list.
    std::ranges::stable_sort(
        found, [&scanner](const Brain* a, const Brain* b) { return scanner.distanceTo(*a) < scanner.distanceTo(*b); });
    for (std::size_t k = kScanMaxEnemies; k < found.size(); ++k) {
        scanner.forget(*found[k]);
    }
    found.resize(std::min(found.size(), kScanMaxEnemies));
    // Each new one is added and told to the scanner (event 0xb).
    int added = 0;
    for (Brain* enemy : found) {
        if (std::ranges::find(scanner.enemies(), enemy) != scanner.enemies().end()) {
            continue;
        }
        scanner.addEnemy(*enemy);
        // The scanner's own handlers: his script's, then his type's (the tactic heard through addEnemy()).
        const BrainEvent seen{.id = kEventEnemyAdded, .other = enemy};
        if (scanner.services() == nullptr || !scanner.services()->humanEvent(scanner, seen)) {
            static_cast<void>(scanner.onEvent(seen));
        }
        ++added;
    }
    return added;
}

// @coney-only setter: the brain's scan interval (`+0x144`), a field the original's goals write directly
void ScanSchedule::setInterval(const Brain& brain, std::uint64_t ms) { m_entries[&brain].intervalMs = ms; }

// @orig 0x0028fa18 Brain_MaybeScanEnemies (unknown)
std::uint64_t ScanSchedule::interval(const Brain& brain) const {
    std::uint64_t ms = brain.type() == BrainType::Cop ? kScanIntervalCopMs : kScanIntervalMs;
    if (const auto found = m_entries.find(&brain); found != m_entries.end() && found->second.intervalMs != 0) {
        ms = found->second.intervalMs;
    }
    // Slower while it fights in the stance.
    if (brain.human().fighter().inFightStance()) {
        ms *= kScanSlowFactor;
    }
    return ms;
}

bool ScanSchedule::maybeScan(const Gangs& gangs, Brain& brain, std::uint64_t nowMs) {
    Entry& entry = m_entries[&brain];
    if (entry.scanned && nowMs < entry.lastMs + interval(brain)) {
        return false;
    }
    if (m_tokens <= 0) {
        return false;
    }
    --m_tokens;
    entry.scanned = true;
    entry.lastMs = nowMs;
    scanEnemies(gangs, brain);
    return true;
}

// @coney-only bookkeeping: drops a removed brain from Coney's schedule map
void ScanSchedule::forget(const Brain& brain) { m_entries.erase(&brain); }

} // namespace coney::ai
