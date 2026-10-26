// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/gangs.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/attack_kinds.h"
#include "ai/script_services.h"
#include "ai/targeting.h"
#include "human/human_flags.h"

namespace coney::ai {

namespace {

// The bit gang `id` has in other gangs' masks.
std::uint32_t bitOf(int id) { return std::uint32_t{1} << static_cast<unsigned>(id); }

// Whether `kind` counts as police for friendship.
bool policeLike(int kind) { return kind == kPoliceKind || kind == kPoliceLikeKind; }

// The handle of `brain`, or the null handle (0) for none.
double handleOf(const Brain* brain) { return brain != nullptr ? brain->handle() : 0.0; }

} // namespace

// The count Gangs::addMember() tests a gang's members against, and the fighter sides' rollover to a new gang.
// @coney-only a named accessor for the member limit Gang_AddMember tests inline
std::size_t Gang::capacity() const { return policeLike(m_kind) ? kGangMemberSlots : kGangMembers; }

std::string_view Gang::handler(int message) const {
    const auto found = m_handlers.find(message);
    return found == m_handlers.end() ? std::string_view{} : std::string_view(found->second);
}

bool downOrDead(const Brain& brain) {
    return brain.human().fighter().health().depleted() || brain.human().state() == human::TargetState::Grounded;
}

bool humanIsDownOrDead(const Brain& brain) {
    // The mask 0x180050000 as Coney models it (Human::downOrDead()): knocked down alive and arrested members stand.
    return brain.human().downOrDead();
}

int Gang::countLiving(bool standingOnly) const {
    if (!standingOnly) {
        return static_cast<int>(m_members.size());
    }
    return static_cast<int>(
        std::ranges::count_if(m_members, [](const Brain* member) { return !humanIsDownOrDead(*member); }));
}

int Gang::standing() const {
    return static_cast<int>(std::ranges::count_if(m_members, [](const Brain* member) { return !downOrDead(*member); }));
}

void Gang::setAlertState(int state) {
    if (state == m_alertState) {
        return;
    }
    m_alertState = state;
    // The clips by state, each under its kind's exception; the turf limit off only while fighting, for the AI only.
    if (state == kGangFighting) {
        if (m_kind != kNoAlertClipsKind) {
            if (m_kind != kNoFidgetsKind) {
                addCombatFidgetClips();
            }
            if (m_kind != kPoliceLikeKind) {
                addFightTauntClips();
            }
        }
    } else if (state == kGangCalm && m_kind != kNoAlertClipsKind) {
        if (m_kind != kNoFidgetsKind) {
            removeCombatFidgetClips();
        }
        if (m_kind != kPoliceLikeKind) {
            removeFightTauntClips();
        }
    }
    for (Brain* member : m_members) {
        if (member->type() != BrainType::Player) {
            member->setKeepsToTurf(state != kGangFighting);
        }
    }
}

int clipGroupSlots(int group) {
    switch (group) {
    case kCombatFidgetClips:
        return 8;
    case kTauntAdvanceClips:
        return 5;
    case kIdleFidgetClips:
    case kCowerClips:
        return 4;
    case kTauntClips:
    case kCheerClips:
        return 3;
    default:
        return 0;
    }
}

int Gang::clipGroup(int group) const {
    const auto found = m_clipGroups.find(group);
    if (found == m_clipGroups.end()) {
        return 0;
    }
    return static_cast<int>(std::ranges::count_if(found->second, [](std::string_view name) { return !name.empty(); }));
}

std::string_view Gang::clip(int group, int slot) const {
    const auto found = m_clipGroups.find(group);
    if (found == m_clipGroups.end() || slot < 0 || static_cast<std::size_t>(slot) >= found->second.size()) {
        return {};
    }
    return found->second[static_cast<std::size_t>(slot)];
}

void Gang::setClipGroup(int group, std::span<const std::string_view> names) {
    const auto slots = static_cast<std::size_t>(clipGroupSlots(group));
    if (slots == 0) {
        return;
    }
    std::vector<std::string_view>& filled = m_clipGroups[group];
    filled.assign(slots, std::string_view{});
    // All of them in order when they fit; else each kept on a 1-in-2 draw while more are left than free slots.
    std::size_t next = 0;
    for (std::size_t i = 0; i < names.size() && next < slots; ++i) {
        const bool take = slots - next >= names.size() - i || rollRange(m_owner->random(), 0, 1) == 1;
        if (take) {
            filled[next++] = names[i];
        }
    }
}

std::string_view Gang::randomClip(int group) {
    const int slots = clipGroupSlots(group);
    if (slots == 0) {
        return {};
    }
    return clip(group, rollRange(m_owner->random(), 0, slots));
}

void Gang::releaseClipGroup(int group) { m_clipGroups.erase(group); }

// The four combat fidgets (table `0x0050cab8`).
void Gang::addCombatFidgetClips() {
    static constexpr std::array<std::string_view, 4> kClips{"combat_fidget_call_front", "combat_fidget_shift",
                                                            "combat_fidget_boxersdance", "combat_fidget_shuffle"};
    setClipGroup(kCombatFidgetClips, kClips);
}

void Gang::removeCombatFidgetClips() { releaseClipGroup(kCombatFidgetClips); }

// Five advancing taunts, a random three of five taunts and three cheers (tables `0x0050caf0`, `0x0050cb08`,
// `0x0050cb20`).
void Gang::addFightTauntClips() {
    static constexpr std::array<std::string_view, 5> kAdvance{"taunt1_adv", "taunt2_adv", "taunt3_adv", "taunt4_adv",
                                                              "taunt5_adv"};
    static constexpr std::array<std::string_view, 5> kTaunts{"gen_tnt_point", "gen_tnt_cheer", "gen_tnt_spit",
                                                             "gen_tnt_wave", "gen_tnt_pointdwn"};
    static constexpr std::array<std::string_view, 3> kCheers{"gen_tnt_cheer1", "gen_tnt_cheer2", "gen_tnt_cheer3"};
    setClipGroup(kTauntAdvanceClips, kAdvance);
    setClipGroup(kTauntClips, kTaunts);
    setClipGroup(kCheerClips, kCheers);
}

void Gang::removeFightTauntClips() {
    releaseClipGroup(kTauntAdvanceClips);
    releaseClipGroup(kTauntClips);
    releaseClipGroup(kCheerClips);
}

bool Gang::onEvent(Brain& member, const BrainEvent& event) {
    // The scripts' message handler, while scripts run.
    ScriptServices* scripts = m_owner->scripts();
    if (const std::string_view function = handler(event.id); scripts != nullptr && !function.empty()) {
        const double other = handleOf(event.other);
        switch (event.id) {
        case kGangMessageDown: {
            const std::array<double, 3> args{member.handle(), other, static_cast<double>(standing())};
            scripts->call(function, args);
            break;
        }
        case kGangMessageHeadcount: {
            const std::array<double, 3> args{member.handle(), other, static_cast<double>(m_members.size())};
            scripts->call(function, args);
            break;
        }
        case kGangMessageArrest: {
            const std::array<double, 3> args{member.handle(), other, static_cast<double>(event.value)};
            scripts->call(function, args);
            break;
        }
        default: {
            const std::array<double, 3> args{member.handle(), other, static_cast<double>(event.value)};
            if (scripts->call(function, args)) {
                return true;
            }
            break;
        }
        }
    }
    // Then the tactic: while the gang has members, or for a member leaving the last place (0x16 with 0). One not yet
    // started is first run once, as the gang's update would (0x00165144): its start marks the members' goal bases and
    // gives its goals, so an event that gives them again (a follow tactic's 0x16 with 1) pops those, not stacks on.
    if (m_tactic == nullptr || (m_members.empty() && (event.id != kGangMessageMembers || event.value != 0))) {
        return false;
    }
    if (!m_tactic->started()) {
        processTactic();
        if (m_tactic == nullptr) {
            return false;
        }
    }
    return m_tactic->event(*this, member, event);
}

void Gang::processTactic() {
    if (const int result = m_tactic->process(*this); result != 0) {
        m_tactic->fireCallback(*this, result);
    }
}

Gangs::Gangs(std::uint32_t seed) : m_random(seed) {
    for (std::size_t slot = 0; slot < m_gangs.size(); ++slot) {
        m_gangs[slot].m_owner = this;
        m_gangs[slot].m_id = static_cast<int>(slot);
    }
}

int Gangs::create(int kind, std::string_view name) {
    const bool taken = std::ranges::any_of(m_gangs, [name](const Gang& g) { return g.m_inUse && g.m_name == name; });
    if (taken) {
        return -1;
    }
    const auto free = std::ranges::find_if(m_gangs, [](const Gang& g) { return !g.m_inUse; });
    if (free == m_gangs.end()) {
        return -1;
    }
    Gang& gang = *free;
    gang.m_inUse = true;
    gang.m_kind = kind;
    gang.m_name = std::string(name);
    gang.m_enemies = 0;
    gang.m_friends = 0;
    gang.m_suspended = false;
    gang.m_invincible = false;
    gang.m_alwaysSeen = false;
    gang.m_members.clear();
    gang.m_tactic.reset();
    gang.m_handlers.clear();
    gang.m_clipGroups.clear();
    gang.m_alertState = kGangCalm;
    gang.m_fleeCount = -1;
    gang.m_canFlee = true;
    return gang.m_id;
}

void Gangs::remove(int id) {
    Gang* gang = find(id);
    if (gang == nullptr) {
        return;
    }
    setTactic(id, nullptr);
    for (Brain* member : gang->m_members) {
        member->setGang(nullptr);
    }
    gang->m_members.clear();
    gang->m_inUse = false;
    // No other gang keeps it as an enemy or a friend.
    for (Gang& other : m_gangs) {
        other.m_enemies &= ~bitOf(id);
        other.m_friends &= ~bitOf(id);
    }
}

Gang* Gangs::find(int id) {
    if (id < 0 || id >= static_cast<int>(kGangSlots)) {
        return nullptr;
    }
    Gang& gang = m_gangs[static_cast<std::size_t>(id)];
    return gang.m_inUse ? &gang : nullptr;
}

const Gang* Gangs::find(int id) const { return const_cast<Gangs*>(this)->find(id); }

int Gangs::countLiving(int id, bool standingOnly) const {
    const Gang* gang = find(id);
    return gang != nullptr ? gang->countLiving(standingOnly) : 0;
}

std::function<int(int gang)> hudGangCount(const Gangs& gangs) {
    // Every HUD count of a gang passes 1: the standing members (docs/research/hud-functions-lua.md#gang-count-what).
    return [&gangs](int gang) { return gangs.countLiving(gang, true); };
}

void Gangs::addMember(int id, Brain& brain) {
    Gang* gang = find(id);
    if (gang == nullptr) {
        return;
    }
    // Leaving the old gang, the same one included: the actions and target cleared, then out of its list under event
    // 0x16 with 0 (a tactic's member popped to his goal base first; a leader no longer leads).
    if (Gang* old = brain.gang(); old != nullptr) {
        static_cast<void>(brain.clearActions());
        brain.setTarget(nullptr);
        if (old->m_tactic != nullptr) {
            brain.popToGoalBase();
        }
        if (old->m_orders.leader == brain.handle()) {
            old->m_orders.leader = 0;
        }
        removeMember(brain);
        static_cast<void>(old->onEvent(brain, BrainEvent{.id = kGangMessageMembers, .other = &brain, .value = 0}));
    }
    if (gang->m_members.size() >= gang->capacity() && !gang->m_members.empty()) {
        removeMember(*gang->m_members.front());
    }
    gang->m_members.push_back(&brain);
    brain.setGang(gang);
    // An invincible gang's new member is invincible too (0x00166308); a player joining leads it (the war chief);
    // then event 0x16 with 1, on which a follow tactic gives every member his goal again.
    if (gang->m_invincible) {
        brain.human().setFlag(human::flag::kGod, true);
    }
    if (brain.type() == BrainType::Player) {
        gang->m_orders.leader = brain.handle();
    }
    static_cast<void>(gang->onEvent(brain, BrainEvent{.id = kGangMessageMembers, .other = &brain, .value = 1}));
}

void Gangs::removeMember(Brain& brain) {
    Gang* gang = brain.gang();
    if (gang == nullptr) {
        return;
    }
    std::erase(gang->m_members, &brain);
    brain.setGang(nullptr);
}

void Gangs::makeEnemies(int a, int b) {
    Gang* first = find(a);
    Gang* second = find(b);
    if (first == nullptr || second == nullptr) {
        return;
    }
    first->m_friends &= ~bitOf(b);
    first->m_enemies |= bitOf(b);
    second->m_friends &= ~bitOf(a);
    second->m_enemies |= bitOf(a);
}

void Gangs::makeFriends(int a, int b) {
    Gang* first = find(a);
    Gang* second = find(b);
    if (first == nullptr || second == nullptr) {
        return;
    }
    first->m_enemies &= ~bitOf(b);
    first->m_friends |= bitOf(b);
    second->m_enemies &= ~bitOf(a);
    second->m_friends |= bitOf(a);
}

bool Gangs::friends(const Gang* a, const Gang* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    if (a == b || a->m_kind == b->m_kind || (policeLike(a->m_kind) && policeLike(b->m_kind)) || a->owner().truce()) {
        return true;
    }
    return (a->m_friends & bitOf(b->m_id)) != 0;
}

void Gangs::makeNeutralOfType(int id, int kind) {
    Gang* gang = find(id);
    if (gang == nullptr) {
        return;
    }
    for (int other = 0; other < static_cast<int>(kGangSlots); ++other) {
        Gang* second = find(other);
        if (second == nullptr || second == gang || second->m_kind != kind || friends(gang, second) ||
            friends(second, gang)) {
            continue;
        }
        gang->m_enemies &= ~bitOf(other);
        gang->m_friends &= ~bitOf(other);
        second->m_enemies &= ~bitOf(id);
        second->m_friends &= ~bitOf(id);
    }
}

void Gangs::makeEnemiesOfType(int id, int kind) {
    Gang* gang = find(id);
    if (gang == nullptr) {
        return;
    }
    for (int other = 0; other < static_cast<int>(kGangSlots); ++other) {
        Gang* second = find(other);
        if (second == nullptr || second->m_kind != kind) {
            continue;
        }
        gang->m_friends &= ~bitOf(other);
        gang->m_enemies |= bitOf(other);
        second->m_friends &= ~bitOf(id);
        second->m_enemies |= bitOf(id);
    }
}

void Gangs::setAlwaysSeen(int id, bool on) {
    if (Gang* gang = find(id); gang != nullptr) {
        gang->m_alwaysSeen = on;
    }
}

bool Gangs::enemies(const Gang* a, const Gang* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    // 1. The police and the police-like kind are never enemies.
    if ((a->m_kind == kPoliceKind && b->m_kind == kPoliceLikeKind) ||
        (a->m_kind == kPoliceLikeKind && b->m_kind == kPoliceKind)) {
        return false;
    }
    // 2. Nobody during a truce.
    if (a->owner().truce()) {
        return false;
    }
    // 3. The bit of b's id in a's mask.
    return (a->m_enemies & bitOf(b->m_id)) != 0;
}

std::optional<int> Gangs::roleOf(const Brain& brain) const {
    return m_lookups.role ? m_lookups.role(brain) : std::nullopt;
}

int Gangs::fleeShare(int kind) const { return m_lookups.fleeShare ? m_lookups.fleeShare(kind) : 0; }

void Gangs::setTruce(std::uint64_t ms) { m_truceEndMs = ms == 0 ? 0 : m_nowMs + ms; }

void Gangs::setThreatResponse(int id, int response) {
    if (Gang* gang = find(id); gang != nullptr) {
        for (Brain* member : gang->m_members) {
            member->setThreatResponse(response);
        }
    }
}

void Gangs::setDead(int id, bool dead) {
    if (Gang* gang = find(id); gang != nullptr) {
        for (Brain* member : gang->m_members) {
            member->setDead(dead);
        }
    }
}

void Gangs::flush(int id) {
    if (Gang* gang = find(id); gang != nullptr) {
        for (Brain* member : gang->m_members) {
            member->flush();
        }
    }
}

void Gangs::suspend(int id, bool suspended) {
    if (Gang* gang = find(id); gang != nullptr) {
        gang->m_suspended = suspended;
    }
}

void Gangs::setInvincible(int id, bool on) {
    Gang* gang = find(id);
    if (gang == nullptr) {
        return;
    }
    gang->m_invincible = on;
    for (Brain* member : gang->m_members) {
        member->human().setFlag(human::flag::kGod, on);
    }
}

void Gangs::setTargetable(int id, bool on) {
    if (const Gang* gang = find(id); gang != nullptr) {
        for (Brain* member : gang->m_members) {
            member->human().script().targetable = on;
        }
    }
}

void Gangs::setAttackable(int id, bool on) {
    if (const Gang* gang = find(id); gang != nullptr) {
        for (Brain* member : gang->m_members) {
            member->setAttackable(on);
        }
    }
}

void Gangs::forget(const Brain& brain) {
    m_scans.forget(brain);
    for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
        if (Gang* gang = find(id); gang != nullptr && gang->m_chosenTarget == &brain) {
            gang->m_chosenTarget = nullptr;
        }
    }
}

void Gangs::setMessageHandler(int id, int message, std::string function) {
    Gang* gang = find(id);
    if (gang == nullptr) {
        return;
    }
    if (function.empty()) {
        gang->m_handlers.erase(message);
    } else {
        gang->m_handlers[message] = std::move(function);
    }
}

void Gangs::setTactic(int id, std::unique_ptr<Tactic> tactic) {
    Gang* gang = find(id);
    if (gang == nullptr) {
        return;
    }
    if (gang->m_tactic != nullptr) {
        gang->m_tactic->finish(*gang);
        // Freed at the end of the next update, as the original queues it: a tactic may be replaced from its own
        // callback.
        m_retired.push_back(std::move(gang->m_tactic));
    }
    gang->m_tactic = std::move(tactic);
    ++gang->m_tacticsSet;
}

void Gangs::update(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    // A new update: the brains that follow may scan again.
    m_scans.beginUpdate();
    for (Gang& gang : m_gangs) {
        if (!gang.m_inUse || gang.m_members.empty() || gang.m_tactic == nullptr) {
            continue;
        }
        gang.processTactic();
    }
    // The tactics replaced since the last update are freed (`0x00306630`).
    m_retired.clear();
}

std::size_t Gang::turfCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_orders.turf, [](double box) { return box != 0.0; }));
}

void Gang::setSharedTarget(Brain* target) {
    // 1. The null target: stored, and nothing else.
    if (target == nullptr) {
        m_chosenTarget = nullptr;
        return;
    }
    // 2. Checked through the leader: none, or one who may not take him, and the old target stays.
    const Brain* lead = leader();
    if (lead == nullptr || !validEnemy(*lead, *target)) {
        return;
    }
    // 3. Never a friend's member.
    if (Gangs::friends(this, target->gang())) {
        return;
    }
    // 4. The target, listed by every AI member; the players are skipped.
    m_chosenTarget = target;
    const std::vector<Brain*> members = m_members; // addEnemy() may tell a tactic, which may change the members
    for (Brain* member : members) {
        if (member->type() != BrainType::Player) {
            member->addEnemy(*target);
        }
    }
}

Brain* Gang::leader() const {
    // Whether `brain` can lead: a member, alive, no player's and on its feet.
    const auto canLead = [](const Brain* brain) {
        return brain != nullptr && brain->type() != BrainType::Player &&
               !brain->human().fighter().health().depleted() && brain->human().state() == human::TargetState::Standing;
    };
    for (Brain* member : m_members) {
        if (member->handle() == m_orders.leader && canLead(member)) {
            return member;
        }
    }
    for (Brain* member : m_members) {
        if (canLead(member)) {
            return member;
        }
    }
    return nullptr;
}

} // namespace coney::ai
