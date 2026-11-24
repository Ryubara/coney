// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Counts the starts of one kind of a brain's action, for a test that watches the brain update by update and sees only
// its front action: the level99 sparring test's taunts and gloats. No game data.

namespace coney::test {

/// Counts the actions of one kind (a taunt, a gloat) a brain starts, from what is at the front of its queue after each
/// update: see() is called once per update with that action when it is of the kind and has started, and with null
/// otherwise.
///
/// An action is new when it was not at the front the update before. Its address alone does not tell: the brain frees
/// a finished action and the next of the kind is often made where it lived, so a count by address missed it in some
/// runs and not others, as the heap reused the address or did not (#1668). Only a new one at the same address at the
/// front the very update after the last would be missed. A reposition taunt never comes so soon: the fight goal waits
/// while any action is queued (src/ai/fight_goal.cpp, its step 7) and each update runs the goals before the actions
/// (Brain::update()), so the update the last action is popped queues nothing behind it. The taunt timer does not rule
/// it out, as it is re-armed when the taunt is queued and may run out while a long one plays. A gloat is queued by an
/// event, not by the goal, so one could in theory follow another that way; the test only prints the gloats.
class ActionStarts {
  public:
    /// Notes this update's front action of the kind (null for none), counting it when it is a new one.
    void see(const void* action) {
        if (action != nullptr && action != m_previous) {
            ++m_count;
        }
        m_previous = action;
    }

    /// The actions counted.
    [[nodiscard]] int count() const { return m_count; }

  private:
    const void* m_previous = nullptr; // the update before's action of the kind; null when none
    int m_count = 0;
};

} // namespace coney::test
