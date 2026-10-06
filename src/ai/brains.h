// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ai/brain.h"
#include "ai/formations.h"
#include "ai/gangs.h"
#include "human/human.h"
#include "human/humans.h"

// The brains of a scene and their step (`Brains_Update`): each enabled brain thinks on one character step in five,
// staggered by its index, and updates on every step, at the brains' place in the characters' step (human::Humans), so
// after the pads have written the player's record and before any human's dispatcher. The formations and the gangs,
// which the original steps just before the brains, are stepped first. Game time comes from the steps run (1/30 s each),
// so a run is the same on every machine. Research: docs/research/ai.md#update, docs/research/tasks.md#humans-update

namespace coney::ai {

/// The brains of a scene, in slot order (the original's 60 brains, `0x006d53f0 + i × 0x2f0`).
class Brains {
  public:
    /// Adds a brain for `human` (not owned; it must outlive the brain, or be removed first) as the next slot, of
    /// `type`, fighting by `settings`, its rolls seeded with `seed`.
    Brain& add(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed);
    /// Removes `human`'s brain: it leaves its gang and formations, and every other brain forgets it. Nothing when it
    /// has none.
    void remove(const human::Human& human);
    /// `human`'s brain; null when it has none.
    [[nodiscard]] Brain* find(const human::Human& human);
    /// Gives every brain, and each added later, `planner` (null for none; it must outlive them or be replaced).
    void setPlanner(RoutePlanner* planner);
    /// The planner setPlanner() gave (null for none).
    [[nodiscard]] RoutePlanner* planner() const { return m_planner; }

    /// One step: the game time advances by one step; the formations, then the gangs (their tactics) step; each enabled
    /// brain whose human is in the world thinks when `index % 5 == step % 5` and updates; then every player brain's
    /// books (its enemies: the brains that target it); last, each human whose health has just run out tells its gang
    /// (event 18, the attacker unknown). **Coney choices**: the books are kept every step (the original counts
    /// attackers every 300 updates); event 18 is sent at the brains' next step after the health runs out (who sends it
    /// in the original is not traced) and gives no attacker.
    /// @orig 0x00293b28 Brains_Update (unknown)
    void update();
    /// What human::Humans runs at the brains' place: update().
    [[nodiscard]] human::Humans::BrainsHook hook();

    /// The distance from `brain`'s human to its nearest enemy with health left, or nothing when it has none: the query
    /// the camera's sprint zoom asks the player's brain (docs/research/camera.md#sprint-zoom).
    [[nodiscard]] static std::optional<float> nearestEnemy(const Brain& brain);

    /// The gangs and the formations of the scene.
    [[nodiscard]] Gangs& gangs() { return m_gangs; }
    [[nodiscard]] const Gangs& gangs() const { return m_gangs; }
    [[nodiscard]] Formations& formations() { return m_formations; }

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

    // Event 18 for each human whose health has run out since the last step.
    void reportDowns();
    // Event 1 for each human whose health fell since the last step (**Coney stand-in**: the original's sender of
    // message 1 is not traced), about the nearest other human that can fight, the damage as its value.
    void reportDamage();

    // Declared before the brains, so they outlive them (a gang's tactic and a formation refer to brains).
    Gangs m_gangs;
    Formations m_formations;
    std::vector<std::unique_ptr<Brain>> m_brains;
    std::uint64_t m_steps = 0;
    RoutePlanner* m_planner = nullptr;
};

} // namespace coney::ai
