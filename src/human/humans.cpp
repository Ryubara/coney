// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/humans.h"

#include <cstddef>

namespace coney::human {

void Humans::add(Human& human, bool padControlled) {
    m_humans.push_back(&human);
    m_padControlled.push_back(padControlled);
}

void Humans::update(const raycast::CollisionMesh* mesh, std::span<TargetHuman* const> targets) {
    // 1. The records: one no pad drives has its command cleared, so only what a brain writes this step is read.
    for (std::size_t i = 0; i < m_humans.size(); ++i) {
        if (!m_padControlled[i]) {
            m_humans[i]->record().command = combat::command::kNone;
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
        Human* human = m_humans[forward ? n : m_humans.size() - 1 - n];
        human->updateActions(targets, mesh);
    }
    ++m_steps;
}

} // namespace coney::human
