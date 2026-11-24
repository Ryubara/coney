// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Counts the starts of one kind of a brain's action, for a test that watches the brain update by update and sees only
// its front action: the level99 sparring test's taunts and gloats. No game data.

namespace coney::test {

/// Counts the actions of one kind (a taunt, a gloat) a brain starts, from what is at the front of its queue after each
/// update: see() is called once per update with that action when it is of the kind and has started, and with null
/// otherwise.
class ActionStarts {
  public:
    /// Notes this update's front action of the kind (null for none), counting it when it is a new one.
    void see(const void* action) {
        if (action != nullptr && action != m_last) {
            m_last = action;
            ++m_count;
        }
    }

    /// The actions counted.
    [[nodiscard]] int count() const { return m_count; }

  private:
    const void* m_last = nullptr; // the action last counted
    int m_count = 0;
};

} // namespace coney::test
