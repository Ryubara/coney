// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ai/brain.h"
#include "ai/formations.h"
#include "ai/gangs.h"
#include "core/engine_limits.h"
#include "human/human.h"
#include "human/humans.h"

// The brains of a scene and their step (`Brains_Update`): each enabled brain thinks on one character step in five,
// staggered by its index, and updates on every step, at the brains' place in the characters' step (human::Humans), so
// after the pads have written the player's record and before any human's dispatcher. The formations and the gangs,
// which the original steps just before the brains, are stepped first. Game time comes from the steps run (1/30 s each),
// so a run is the same on every machine. Research: docs/research/ai/index.md#update,
// docs/research/tasks.md#humans-update

namespace coney::ai {

/// `seed` moved by a run's `variation` (`--ai-seed`): the same seed for variation 0, else the seed plus the variation
/// times an odd constant (the golden ratio's 32 bits), so two slots' seeds stay apart. **Coney's own**: the original's
/// brains draw from the one shared stream (docs/research/scripting.md, `0x006eb880`), which a recording varies by its
/// starting index; Coney's brains each roll their own generator, so a run varies them through their seeds.
/// @coney-only the `--ai-seed` variation; the original's brains share one stream
[[nodiscard]] constexpr std::uint32_t variedSeed(std::uint32_t seed, std::uint32_t variation) {
    return seed + (variation * 0x9e3779b9U);
}

/// The brains of a scene, in slot order (the original's 60 brains, `0x006d53f0 + i × 0x2f0`).
class Brains {
  public:
    /// No brains; every brain's and the gangs' rolls seeded through variedSeed() with `variation` (0: as always).
    explicit Brains(std::uint32_t variation = 0);
    /// Takes its slots off the humans it leaves (they outlive their brains), so none points into a table that is gone.
    /// @coney-only bookkeeping of Coney's brain table
    ~Brains();
    // Not copied or moved: the humans keep slots into this one table, which a copy would share and a move would leave.
    Brains(const Brains&) = delete;
    Brains& operator=(const Brains&) = delete;
    Brains(Brains&&) = delete;
    Brains& operator=(Brains&&) = delete;
    /// Adds a brain for `human` (not owned; it must outlive the brain, or be removed first) as the next slot, of
    /// `type`, fighting by `settings`, its rolls seeded with `seed`.
    /// @coney-only Coney's table of brains; the original fills one of its 60 fixed slots, in code not traced here
    Brain& add(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed);
    /// Removes `human`'s brain: it leaves its gang and formations, and every other brain forgets it. Nothing when it
    /// has none.
    /// @coney-only Coney's table of brains; the original frees a fixed slot, in code not traced here
    void remove(const human::Human& human);
    /// `human`'s brain (his first in slot order, when he has two); null when he has none. Constant time: the slot is
    /// kept on the human (human::Human::brainSlot()), as the opposition test between every two humans asks it for both,
    /// many times a step (docs/guides/performance.md#brain-lookup). A human with brains in two tables at once (only
    /// tests make one) holds the slot of one; the other finds his brain by a walk over its slots.
    /// @coney-only lookup in Coney's brain table; the original reaches a human's brain through its own pointer
    [[nodiscard]] Brain* find(const human::Human& human);
    /// Gives every brain, and each added later, `planner` (null for none; it must outlive them or be replaced).
    /// @coney-only wiring of Coney's route planner
    void setPlanner(RoutePlanner* planner);
    /// The planner setPlanner() gave (null for none).
    [[nodiscard]] RoutePlanner* planner() const { return m_planner; }
    /// Gives every brain, and each added later, the level's collision for its sight rays (null for none).
    /// @coney-only wiring of the level's collision
    void setCollision(const raycast::CollisionMesh* collision);
    /// The collision setCollision() gave (null for none).
    [[nodiscard]] const raycast::CollisionMesh* collision() const { return m_collision; }
    /// Gives the gangs and every brain, and each added later, the engine limits preset (core/engine_limits.h): the
    /// rule each lifted limit of the scene follows. Coney's own rule until set.
    /// @coney-only the engine limits preset, a test-mode switch
    void setEngineLimits(EngineLimits limits);
    /// The engine limits preset setEngineLimits() gave.
    [[nodiscard]] EngineLimits engineLimits() const { return EngineLimits::Coney; }

    /// One step: the game time advances by one step; the formations, then the gangs (their tactics) step; each enabled
    /// brain whose human is in the world thinks when `index % 5 == step % 5` and updates; then every player brain's
    /// books (its enemies: the brains that target it); last, each human whose health has just run out tells its gang
    /// (event 18, the attacker unknown). **Coney choices**: the books are kept every step (the original counts
    /// attackers every 300 updates); event 18 is sent at the brains' next step after the health runs out (who sends it
    /// in the original is not traced) and gives no attacker.
    /// @orig 0x00293b28 Brains_Update (unknown)
    void update();
    /// What human::Humans runs at the brains' place: update().
    /// @coney-only glue between Coney's humans' step and the brains
    [[nodiscard]] human::Humans::BrainsHook hook();

    /// The distance from `brain`'s human to its nearest enemy with health left, or nothing when it has none: the query
    /// the camera's sprint zoom asks the player's brain (docs/research/camera.md#sprint-zoom).
    /// The part of the follow camera's update that reads the player's brain, kept beside the brains it walks.
    /// Stand-in: only enemies with health left, measured across the ground; the original's latch takes each of the
    /// brain's 16 enemy handles that still resolves and its squared 3D distance (0x004db5b0).
    /// @orig 0x0012ae58 Cam_Follow_Update (Cam_Follow.cpp)
    [[nodiscard]] static std::optional<float> nearestEnemy(const Brain& brain);

    /// The gangs of the scene.
    [[nodiscard]] Gangs& gangs() { return m_gangs; }
    /// The gangs of the scene, to read.
    [[nodiscard]] const Gangs& gangs() const { return m_gangs; }
    /// The formations of the scene.
    [[nodiscard]] Formations& formations() { return m_formations; }

    /// The variation the seeds are moved by (variedSeed()).
    [[nodiscard]] std::uint32_t variation() const { return m_variation; }
    /// Steps run.
    [[nodiscard]] std::uint64_t steps() const { return m_steps; }
    /// The game time of the last step, ms.
    [[nodiscard]] std::uint64_t nowMs() const { return m_steps * 1000 / 30; }
    /// How many brains there are.
    [[nodiscard]] std::size_t size() const { return m_brains.size(); }
    /// The brain in slot `index` (below size()).
    [[nodiscard]] Brain& at(std::size_t index) { return *m_brains[index]; }
    /// The brain in slot `index` (below size()), to read.
    [[nodiscard]] const Brain& at(std::size_t index) const { return *m_brains[index]; }

  private:
    // Whether the slot on `human` is this table's: true unless he is one of m_unlinked.
    // @coney-only bookkeeping of Coney's brain table
    [[nodiscard]] bool linked(const human::Human& human) const;
    // The slot of `human`'s first brain here, by a walk over the slots; nothing for none.
    // @coney-only bookkeeping of Coney's brain table
    [[nodiscard]] std::optional<std::size_t> firstSlotOf(const human::Human& human) const;

    // A player brain's books: its enemies are the brains whose target it is.
    // @coney-only Coney's choice of keeping them every step; the original recounts them every 300 updates
    // (docs/research/ai-brain-types.md#think-player)
    void keepBooks(Brain& player);

    // Event 18 for each human whose health has run out since the last step.
    // @coney-only stand-in for the event's sender, which is not traced (update() says so)
    void reportDowns();
    // Event kEventPutDown for each human who has come to lie on the ground since the last step: a knockdown's
    // reaction over (`HitReact_OnSpecialEnd`) or a throw, grab or mount letting go of him (`Throw_ClearVictimLink`),
    // not knocked out. **Coney stand-in**: both read as "grounded, not held, his health not out and no reaction clip
    // driving him", once per time down; the original sends from the two places themselves.
    // @coney-only stand-in poll for the event HitReact_OnSpecialEnd and Throw_ClearVictimLink send
    void reportPutDowns();
    // The health each human lost since the last step, kept before the gangs' tactics run: a tactic that holds a boss
    // above his cap gives the health back at once, and the hit must still reach reportDamage().
    // @coney-only bookkeeping for the reportDamage() stand-in
    void noteDamage();
    // Event 1 for each human whose health fell since the last step, about the nearest other human that can fight, the
    // damage as its value; while it is sent his hit timer (record +0x108) reads as this function last saw it.
    // **Coney stand-in**: the original sends it at the strike (Human_DealDamage 0x0021d680) with the attacker, before
    // the victim's update applies the hit and writes the timer (docs/research/characters.md#power-class-bytes).
    // @coney-only stand-in for Human_DealDamage's event 1, sent after the hit is applied since the attacker is not kept
    void reportDamage();

    // Declared before the brains, so they outlive them (a gang's tactic and a formation refer to brains).
    Gangs m_gangs;
    Formations m_formations;
    std::vector<std::unique_ptr<Brain>> m_brains;
    // Each slot's human, beside m_brains: find() checks a human's slot here, in one small array, rather than in the
    // brain, whose object is a cache miss of its own for every lookup.
    std::vector<const human::Human*> m_humanOf;
    // The humans with a brain here whose slot (human::Human::brainSlot()) another table holds; empty unless a human has
    // brains in two tables at once. Every other human with a brain here holds the slot of his first one.
    std::vector<const human::Human*> m_unlinked;
    std::uint64_t m_steps = 0;
    std::uint32_t m_variation = 0;
    RoutePlanner* m_planner = nullptr;
    const raycast::CollisionMesh* m_collision = nullptr;
};

} // namespace coney::ai
