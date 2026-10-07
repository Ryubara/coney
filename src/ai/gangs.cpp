// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/gangs.h"

#include <algorithm>
#include <array>
#include <utility>

#include "ai/script_services.h"
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

std::string_view Gang::handler(int message) const {
    const auto found = m_handlers.find(message);
    return found == m_handlers.end() ? std::string_view{} : std::string_view(found->second);
}

int Gang::standing() const {
    return static_cast<int>(std::ranges::count_if(m_members, [](const Brain* member) {
        return !member->human().fighter().health().depleted() &&
               member->human().state() != human::TargetState::Grounded;
    }));
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
    // Then the tactic, while the gang has members.
    if (m_tactic != nullptr && !m_members.empty()) {
        return m_tactic->event(*this, member, event);
    }
    return false;
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

void Gangs::addMember(int id, Brain& brain) {
    Gang* gang = find(id);
    if (gang == nullptr || brain.gang() == gang) {
        return;
    }
    // Leaving the old gang flushes what the brain was doing.
    if (brain.gang() != nullptr) {
        removeMember(brain);
        brain.flush();
    }
    const std::size_t cap = policeLike(gang->m_kind) ? kGangMemberSlots : kGangMembers;
    if (gang->m_members.size() >= cap && !gang->m_members.empty()) {
        removeMember(*gang->m_members.front());
    }
    gang->m_members.push_back(&brain);
    brain.setGang(gang);
    // An invincible gang's new member is invincible too (0x00166308).
    if (gang->m_invincible) {
        brain.human().setFlag(human::flag::kGod, true);
    }
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
    if (a == b || a->m_kind == b->m_kind || (policeLike(a->m_kind) && policeLike(b->m_kind))) {
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
    return a != nullptr && b != nullptr && (a->m_enemies & bitOf(b->m_id)) != 0;
}

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
}

void Gangs::update(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    for (Gang& gang : m_gangs) {
        if (!gang.m_inUse || gang.m_members.empty() || gang.m_tactic == nullptr) {
            continue;
        }
        if (const int result = gang.m_tactic->process(gang); result != 0) {
            gang.m_tactic->fireCallback(gang, result);
        }
    }
    // The tactics replaced since the last update are freed (`0x00306630`).
    m_retired.clear();
}

std::size_t Gang::turfCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_orders.turf, [](double box) { return box != 0.0; }));
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
