// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ai/brain.h"
#include "human/human.h"
#include "human/humans.h"

// The brains of a scene and their step (`Brains_Update`): each enabled brain thinks on one character step in five,
// staggered by its index, and updates on every step, at the brains' place in the characters' step (human::Humans), so
// after the pads have written the player's record and before any human's dispatcher. Game time comes from the steps
// run (1/30 s each), so a run is the same on every machine.
// Research: docs/research/ai.md#update, docs/research/tasks.md#humans-update

namespace coney::ai {

/// The brains of a scene, in slot order (the original's 60 brains, `0x006d53f0 + i × 0x2f0`).
class Brains {
  public:
    /// Adds a brain for `human` (not owned; it must outlive the brain, or be removed first) as the next slot, of
    /// `type`, fighting by `settings`, its rolls seeded with `seed`.
    Brain& add(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed);
    /// Removes `human`'s brain; every other brain forgets it. Nothing when it has none.
    void remove(const human::Human& human);
    /// `human`'s brain; null when it has none.
    [[nodiscard]] Brain* find(const human::Human& human);

    /// One step: the game time advances by one step, then each enabled brain whose human is in the world thinks when
    /// `index % 5 == step % 5` and updates; then every player brain's books (its enemies: the brains that target it).
    /// **Coney choice**: the books are kept every step (the original counts attackers every 300 updates).
    /// @orig 0x00293b28 Brains_Update (unknown)
    void update();
    /// What human::Humans runs at the brains' place: update().
    [[nodiscard]] human::Humans::BrainsHook hook();

    /// The distance from `brain`'s human to its nearest enemy with health left, or nothing when it has none: the query
    /// the camera's sprint zoom asks the player's brain (docs/research/camera.md#sprint-zoom).
    [[nodiscard]] static std::optional<float> nearestEnemy(const Brain& brain);

    /// Steps run.
    [[nodiscard]] std::uint64_t steps() const { return m_steps; }
    /// The game time of the last step, ms.
    [[nodiscard]] std::uint64_t nowMs() const { return m_steps * 1000 / 30; }
    [[nodiscard]] std::size_t size() const { return m_brains.size(); }
    [[nodiscard]] Brain& at(std::size_t index) { return *m_brains[index]; }
    [[nodiscard]] const Brain& at(std::size_t index) const { return *m_brains[index]; }

  private:
    // A player brain's books: its enemies are the brains whose target it is.
    void keepBooks(Brain& player);

    std::vector<std::unique_ptr<Brain>> m_brains;
    std::uint64_t m_steps = 0;
};

} // namespace coney::ai
