// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/humans.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace coney::human {

void Humans::add(Human& human, bool padControlled) {
    m_humans.push_back(&human);
    m_padControlled.push_back(padControlled);
    m_fightsTargets.push_back(padControlled);
}

void Humans::setPadControlled(const Human& human, bool padControlled) {
    const auto found = std::ranges::find(m_humans, &human);
    if (found != m_humans.end()) {
        m_padControlled[static_cast<std::size_t>(std::distance(m_humans.begin(), found))] = padControlled;
    }
}

bool Humans::opposed(std::size_t slot, std::size_t other) const {
    if (m_opposition) {
        return m_opposition(*m_humans[slot], *m_humans[other]);
    }
    return m_fightsTargets[slot] != m_fightsTargets[other];
}

void Humans::remove(const Human& human) {
    const auto found = std::ranges::find(m_humans, &human);
    if (found == m_humans.end()) {
        return;
    }
    const auto slot = std::distance(m_humans.begin(), found);
    m_humans.erase(found);
    m_padControlled.erase(m_padControlled.begin() + slot);
    m_fightsTargets.erase(m_fightsTargets.begin() + slot);
}

void Humans::gatherTargets(std::size_t slot, std::span<Combatant* const> targets) {
    m_scratch.clear();
    if (m_fightsTargets[slot]) {
        m_scratch.assign(targets.begin(), targets.end());
    }
    for (std::size_t other = 0; other < m_humans.size(); ++other) {
        if (other != slot && opposed(slot, other)) {
            m_scratch.push_back(m_humans[other]);
        }
    }
}

void Humans::update(const raycast::CollisionMesh* mesh, std::span<Combatant* const> targets) {
    // 1. The records: one no pad drives has its command cleared, so only what a brain writes this step is read, and no
    // buttons, which only a pad gives.
    for (std::size_t i = 0; i < m_humans.size(); ++i) {
        if (!m_padControlled[i]) {
            m_humans[i]->record().command = combat::command::kNone;
            m_humans[i]->record().buttons = 0;
        }
    }
    // 2. The brains write their humans' records.
    if (m_brains) {
        m_brains(m_humans);
    }
    // 3. Every human's animation, then 4. every human's state update.
    for (Human* human : m_humans) {
        human->animate(mesh);
    }
    for (Human* human : m_humans) {
        human->updateState(mesh);
    }
    // 5. Every human's actions, first to last on one step and last to first on the next.
    const bool forward = m_steps % 2 == 0;
    for (std::size_t n = 0; n < m_humans.size(); ++n) {
        const std::size_t slot = forward ? n : m_humans.size() - 1 - n;
        gatherTargets(slot, targets);
        m_humans[slot]->updateActions(m_scratch, mesh);
    }
    ++m_steps;
}

} // namespace coney::human
