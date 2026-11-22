// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/humans.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <utility>

#include "core/perf.h"

namespace coney::human {

// @coney-only bookkeeping: Coney's list of the humans in its characters' step
void Humans::add(Human& human, bool padControlled) {
    human.setFaceRules(m_faceRules.get());
    m_humans.push_back(&human);
    m_padControlled.push_back(padControlled);
    m_fightsTargets.push_back(padControlled);
    m_held.push_back(false);
    m_slotOf[static_cast<const Combatant*>(&human)] = m_humans.size() - 1;
}

// @coney-only bookkeeping: which humans of Coney's step a pad drives
void Humans::setPadControlled(const Human& human, bool padControlled) {
    const auto found = std::ranges::find(m_humans, &human);
    if (found != m_humans.end()) {
        m_padControlled[static_cast<std::size_t>(std::distance(m_humans.begin(), found))] = padControlled;
    }
}

// @coney-only debug tooling: the Entity inspector's freeze
void Humans::setHeld(const Human& human, bool held) {
    const auto found = std::ranges::find(m_humans, &human);
    if (found != m_humans.end()) {
        m_held[static_cast<std::size_t>(std::distance(m_humans.begin(), found))] = held;
    }
}

// @coney-only debug tooling: the Entity inspector's freeze
bool Humans::held(const Human& human) const {
    const auto found = std::ranges::find(m_humans, &human);
    return found != m_humans.end() && m_held[static_cast<std::size_t>(std::distance(m_humans.begin(), found))];
}

GravityTuning& gravityTuning() {
    static GravityTuning tuning;
    return tuning;
}

void Humans::pullByGravity(float gravity) {
    if (gravity == Human::kGravity) {
        return;
    }
    for (std::size_t slot = 0; slot < m_humans.size(); ++slot) {
        if (!m_held[slot]) {
            m_humans[slot]->addGravityPull(gravity);
        }
    }
}

// @coney-only glue: who fights whom in Coney's step, from the gangs' hook or the pad split
bool Humans::opposed(std::size_t slot, std::size_t other) const {
    if (m_opposition) {
        return m_opposition(*m_humans[slot], *m_humans[other]);
    }
    return m_fightsTargets[slot] != m_fightsTargets[other];
}

// @coney-only bookkeeping: takes a human out of Coney's step lists
void Humans::remove(const Human& human) {
    const auto found = std::ranges::find(m_humans, &human);
    if (found == m_humans.end()) {
        return;
    }
    (*found)->setBodyContact(nullptr);
    (*found)->setObjectPush(nullptr);
    (*found)->setHumanContacts({});
    (*found)->setContactSource(nullptr);
    (*found)->setTargetSource(nullptr);
    // The others' list this step no longer holds it.
    std::ranges::replace(m_contactList, *found, nullptr);
    const auto slot = std::distance(m_humans.begin(), found);
    m_humans.erase(found);
    m_padControlled.erase(m_padControlled.begin() + slot);
    m_fightsTargets.erase(m_fightsTargets.begin() + slot);
    m_held.erase(m_held.begin() + slot);
    // The slots behind it moved: the slot table follows, and the grid is built again at its next use.
    m_slotOf.clear();
    for (std::size_t i = 0; i < m_humans.size(); ++i) {
        m_slotOf[static_cast<const Combatant*>(m_humans[i])] = i;
    }
    m_grid.valid = false;
}

namespace {

// How far beyond a search's reach the grid looks: a human may have moved this far since the grid was built before it
// is built again (keepTargetGrid()).
constexpr float kGridMargin = 2.5F;
// The grid's cell, and the most cells along one axis; the cells grow to fit a wider crowd.
constexpr float kGridCell = 2.5F;
constexpr int kGridMaxCells = 256;
// A reach the grid does not answer: every slot is looked at.
constexpr float kGridMaxReach = 1.0e6F;

} // namespace

// @coney-only Coney's broad phase for the target searches (issue 1550)
void Humans::buildTargetGrid() {
    TargetGrid& grid = m_grid;
    grid.valid = false;
    grid.count = m_humans.size();
    grid.places.clear();
    grid.widest = 0.0F;
    if (m_humans.empty()) {
        return;
    }
    float minX = std::numeric_limits<float>::max();
    float minY = minX;
    float maxX = -minX;
    float maxY = -minX;
    for (const Human* human : m_humans) {
        const anim::Vec3 at = human->position();
        if (!std::isfinite(at.x) || !std::isfinite(at.y)) {
            return;
        }
        minX = std::min(minX, at.x);
        maxX = std::max(maxX, at.x);
        minY = std::min(minY, at.y);
        maxY = std::max(maxY, at.y);
        grid.places.push_back(at);
        grid.widest = std::max(grid.widest, human->capsuleRadius());
    }
    grid.cell = kGridCell;
    const float extent = std::max(maxX - minX, maxY - minY);
    if (extent / grid.cell > static_cast<float>(kGridMaxCells - 1)) {
        grid.cell = extent / static_cast<float>(kGridMaxCells - 1);
    }
    grid.minX = minX;
    grid.minY = minY;
    grid.nx = static_cast<int>((maxX - minX) / grid.cell) + 1;
    grid.ny = static_cast<int>((maxY - minY) / grid.cell) + 1;
    const std::size_t cells = static_cast<std::size_t>(grid.nx) * static_cast<std::size_t>(grid.ny);
    grid.start.assign(cells + 1, 0);
    const auto cellOf = [&grid](const anim::Vec3& at) {
        const int cx = std::clamp(static_cast<int>((at.x - grid.minX) / grid.cell), 0, grid.nx - 1);
        const int cy = std::clamp(static_cast<int>((at.y - grid.minY) / grid.cell), 0, grid.ny - 1);
        return (static_cast<std::size_t>(cy) * static_cast<std::size_t>(grid.nx)) + static_cast<std::size_t>(cx);
    };
    // A counting sort by cell keeps each cell's slots ascending.
    for (const anim::Vec3& at : grid.places) {
        ++grid.start[cellOf(at) + 1];
    }
    for (std::size_t c = 0; c < cells; ++c) {
        grid.start[c + 1] += grid.start[c];
    }
    grid.items.assign(m_humans.size(), 0);
    std::vector<std::uint32_t> next(grid.start.begin(), grid.start.end() - 1);
    for (std::size_t slot = 0; slot < grid.places.size(); ++slot) {
        grid.items[next[cellOf(grid.places[slot])]++] = static_cast<std::uint32_t>(slot);
    }
    m_marks.assign((m_humans.size() + 63) / 64, 0);
    grid.valid = true;
}

// @coney-only Coney's broad phase for the target searches (issue 1550)
void Humans::keepTargetGrid(std::size_t slot) {
    if (!m_grid.valid || slot >= m_humans.size()) {
        return;
    }
    // A human gone further than the margin from his place in the grid could be within a search's reach and outside
    // the cells it looks at. The actions move the acting human and the one he holds (a grab's snap,
    // Human_MoveAttached); nothing else moves in the pass.
    const auto strayed = [this](std::size_t which) {
        if (which >= m_grid.count) {
            return false;
        }
        const anim::Vec3 at = m_humans[which]->position();
        const anim::Vec3& was = m_grid.places[which];
        const float dx = at.x - was.x;
        const float dy = at.y - was.y;
        return !((dx * dx) + (dy * dy) <= kGridMargin * kGridMargin);
    };
    bool rebuild = strayed(slot);
    if (const Holdable* held = m_humans[slot]->fighter().held(); !rebuild && held != nullptr) {
        const auto found = m_slotOf.find(static_cast<const Combatant*>(held));
        rebuild = found != m_slotOf.end() && strayed(found->second);
    }
    if (rebuild) {
        buildTargetGrid();
    }
}

// @coney-only Coney's broad phase for the target searches (issue 1550)
bool Humans::markNear(const anim::Vec3& at, float reach) {
    const TargetGrid& grid = m_grid;
    if (!grid.valid || !(reach < kGridMaxReach) || !std::isfinite(at.x) || !std::isfinite(at.y)) {
        return false;
    }
    const float r = reach + kGridMargin;
    const auto lowX = static_cast<int>(std::floor((at.x - r - grid.minX) / grid.cell));
    const auto highX = static_cast<int>(std::floor((at.x + r - grid.minX) / grid.cell));
    const auto lowY = static_cast<int>(std::floor((at.y - r - grid.minY) / grid.cell));
    const auto highY = static_cast<int>(std::floor((at.y + r - grid.minY) / grid.cell));
    const int x0 = std::clamp(lowX, 0, grid.nx); // x0 == x1 + 1 when the window misses the grid: no cell
    const int x1 = std::clamp(highX, -1, grid.nx - 1);
    const int y0 = std::clamp(lowY, 0, grid.ny);
    const int y1 = std::clamp(highY, -1, grid.ny - 1);
    for (int y = y0; y <= y1; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.nx);
        const std::uint32_t begin = grid.start[row + static_cast<std::size_t>(x0)];
        const std::uint32_t end = grid.start[row + static_cast<std::size_t>(x1) + 1];
        for (std::uint32_t i = begin; i < end; ++i) {
            const std::uint32_t slot = grid.items[i];
            m_marks[slot >> 6U] |= std::uint64_t{1} << (slot & 63U);
        }
    }
    return true;
}

// @coney-only Coney's broad phase for the target searches (issue 1550)
template <class Visit> void Humans::visitMarked(const Visit& visit) {
    for (std::size_t word = 0; word < m_marks.size(); ++word) {
        std::uint64_t bits = std::exchange(m_marks[word], 0);
        while (bits != 0) {
            const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
            bits &= bits - 1;
            const std::size_t slot = (word * 64) + bit;
            if (slot < m_humans.size()) {
                visit(slot);
            }
        }
    }
    for (std::size_t slot = m_grid.count; slot < m_humans.size(); ++slot) {
        visit(slot);
    }
}

// @coney-only Coney's per-slot target query (issue 1550)
void Humans::SlotTargets::begin(Humans& owner, std::size_t slot, std::span<Combatant* const> passive) {
    m_owner = &owner;
    m_slot = slot;
    m_passive = m_owner->m_fightsTargets[slot] ? passive : std::span<Combatant* const>{};
    m_radius = -1.0F;
    m_near.clear();
}

// @coney-only Coney's per-slot target query (issue 1550); the original's walk (Humans_FindInRange 0x00227598) tests
// the distance before the search's filter, as this tests it before opposed()
std::span<Combatant* const> Humans::SlotTargets::within(const anim::Vec3& from, float radius) const {
    // The last answer covers a search from the same place within a radius no larger.
    if (m_radius >= radius && from.x == m_from.x && from.y == m_from.y) {
        return m_near;
    }
    Humans& owner = *m_owner;
    m_near.assign(m_passive.begin(), m_passive.end());
    // The grid's cells hold several times the humans within the radius, so one further than it across the ground is
    // dropped before the AI is asked whether the two fight (the searches would drop him themselves). The test leaves
    // a hair of room so that no rounding drops one the search's own `distance <= reach` keeps.
    const float limit = (radius * 1.001F) + 0.01F;
    const auto take = [this, &owner, &from, limit](std::size_t other) {
        if (other == m_slot) {
            return;
        }
        const anim::Vec3 at = owner.m_humans[other]->position();
        const float dx = at.x - from.x;
        const float dy = at.y - from.y;
        if ((dx * dx) + (dy * dy) > limit * limit) {
            return;
        }
        if (owner.opposed(m_slot, other)) {
            m_near.push_back(owner.m_humans[other]);
        }
    };
    if (owner.markNear(from, radius)) {
        owner.visitMarked(take);
    } else {
        for (std::size_t other = 0; other < owner.m_humans.size(); ++other) {
            take(other);
        }
    }
    m_radius = radius;
    m_from = from;
    return m_near;
}

// @coney-only Coney's per-slot target query (issue 1550)
bool Humans::SlotTargets::listed(const Combatant* target) const {
    if (std::ranges::find(m_passive, target) != m_passive.end()) {
        return true;
    }
    const auto found = m_owner->m_slotOf.find(target);
    return found != m_owner->m_slotOf.end() && found->second != m_slot && m_owner->opposed(m_slot, found->second);
}

// @coney-only Coney's per-slot target query (issue 1550): the stance's enemy filter (`TargetFilter_StanceEnemy`,
// 0x002791b0) refuses a human that is no threat and a pad player (per-player +0x1b), whom the attacks may still pick
bool Humans::SlotTargets::calm(const Combatant* target) const {
    const Humans& owner = *m_owner;
    const auto found = owner.m_slotOf.find(target);
    if (found == owner.m_slotOf.end() || found->second == m_slot || !owner.opposed(m_slot, found->second)) {
        return false;
    }
    const std::size_t other = found->second;
    const bool threat = !owner.m_threat || owner.m_threat(*owner.m_humans[m_slot], *owner.m_humans[other]);
    return !threat || owner.m_padControlled[other];
}

// @coney-only Coney's broad phase for the contacts (issue 1556)
std::span<Human* const> Humans::SlotContacts::near(const anim::Vec3& at, float reach) const {
    Humans& owner = *m_owner;
    // The grid holds where each human stood; the widest radius covers his capsule's.
    if (!owner.markNear(at, reach + owner.m_grid.widest)) {
        return owner.m_contactList;
    }
    m_near.clear();
    // A slot added since the list was made is not in it; one removed leaves the grid unbuilt.
    owner.visitMarked([this, &owner](std::size_t other) {
        if (other < owner.m_contactList.size() && owner.m_contactList[other] != nullptr) {
            m_near.push_back(owner.m_contactList[other]);
        }
    });
    return m_near;
}

// @coney-only Coney's broad phase for the strikes (issue 1556)
void Humans::gatherVictims(std::size_t slot) {
    m_victims.clear();
    const anim::Vec3 striker = m_humans[slot]->position();
    // A hair of room, so that no rounding drops one the strike test's own `distance <= 3 m` keeps.
    const float limit = (kStrikeBroadReach * 1.001F) + 0.01F;
    const auto take = [this, slot, &striker, limit](std::size_t other) {
        if (other == slot) {
            return;
        }
        const anim::Vec3 at = m_humans[other]->position();
        const float dx = at.x - striker.x;
        const float dy = at.y - striker.y;
        if ((dx * dx) + (dy * dy) > limit * limit) {
            return;
        }
        if (opposed(slot, other)) {
            m_victims.push_back(m_humans[other]);
        }
    };
    if (markNear(striker, limit)) {
        visitMarked(take);
    } else {
        for (std::size_t other = 0; other < m_humans.size(); ++other) {
            take(other);
        }
    }
}

void Humans::update(const raycast::CollisionMesh* mesh, std::span<Combatant* const> targets, float stepSeconds) {
    // The step every human advances by this time.
    for (Human* human : m_humans) {
        human->setStepSeconds(stepSeconds);
        human->setBodyContact(m_bodyContact ? &m_bodyContact : nullptr);
        human->setObjectPush(m_objectPush ? &m_objectPush : nullptr);
    }
    // 1. The records: one no pad drives has its command cleared, so only what a brain writes this step is read, and no
    // buttons, which only a pad gives.
    for (std::size_t i = 0; i < m_humans.size(); ++i) {
        m_humans[i]->record().padDriven = m_padControlled[i];
        if (!m_padControlled[i]) {
            m_humans[i]->record().command = combat::command::kNone;
            m_humans[i]->record().padCommand = combat::command::kNone;
            m_humans[i]->record().climbToward.reset();
            m_humans[i]->record().buttons = 0;
        }
    }
    // 2. The brains write their humans' records.
    if (m_brains) {
        CONEY_PERF_ZONE("humans.brains");
        m_brains(m_humans);
    }
    // 3. Every human's animation, then 4. every human's state update; a held human (setHeld()) takes no part in 3-5.
    {
        CONEY_PERF_ZONE("humans.animate");
        for (std::size_t slot = 0; slot < m_humans.size(); ++slot) {
            if (!m_held[slot]) {
                m_humans[slot]->animate(mesh);
            }
        }
    }
    // Every walking body meets the others' capsules (docs/research/physics.md#human-contacts), from a list of its own
    // so that a human added during the pass does not move it.
    m_contactList = m_humans;
    for (Human* human : m_humans) {
        human->setHumanContacts(m_contactList);
    }
    // Each human's strike test runs in its state update, after its move (the humans it fights, and the level's
    // objects). The humans a walking body meets (SlotContacts) and a strike's victims (gatherVictims()) come from a
    // grid of where every human stands as the pass starts, rather than every human (issue 1556).
    buildTargetGrid();
    m_slotContacts.begin(*this);
    {
        CONEY_PERF_ZONE("humans.state");
        for (std::size_t slot = 0; slot < m_humans.size(); ++slot) {
            if (m_held[slot]) {
                continue;
            }
            Human* const walker = m_humans[slot];
            walker->setContactSource(&m_slotContacts);
            m_humans[slot]->updateState(mesh);
            walker->setContactSource(nullptr);
            if (m_humans[slot]->strikeShapes().anyOn()) {
                gatherVictims(slot);
                m_humans[slot]->testStrikes(m_victims, m_strikeContact ? &m_strikeContact : nullptr);
            }
            keepTargetGrid(slot);
        }
    }
    // 5. Every human's actions, first to last on one step and last to first on the next.
    const bool forward = m_steps % 2 == 0;
    CONEY_PERF_ZONE("humans.actions");
    // Each human's searches ask for the targets near him (SlotTargets), from a grid of where every human stands as
    // the pass starts, rather than a list of every human he fights.
    buildTargetGrid();
    for (std::size_t n = 0; n < m_humans.size(); ++n) {
        const std::size_t slot = forward ? n : m_humans.size() - 1 - n;
        if (m_held[slot]) {
            continue;
        }
        Human& human = *m_humans[slot];
        m_slotTargets.begin(*this, slot, targets);
        human.setTargetSource(&m_slotTargets);
        human.updateActions({}, mesh);
        human.setTargetSource(nullptr);
        keepTargetGrid(slot);
    }
    m_grid.valid = false;
    ++m_steps;
}

} // namespace coney::human
