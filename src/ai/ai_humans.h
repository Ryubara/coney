// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "animation/anim_math.h"
#include "human/human.h"
#include "human/player.h"
#include "human/target_human.h"
#include "raycast/collision_mesh.h"

// The AI humans of a scene where the player plays: each a human with its own brain, stepped in the player's
// characters' step (on the enemy side) with the brains at their place in it, and the player's own brain (type 0) that
// keeps his books (his enemies, which the camera's sprint zoom asks about). Front-end neutral: the play mode draws the
// humans from their snapshots, and the sandbox, the debug menus and the tests spawn them here.
// Research: docs/research/ai.md, docs/research/tasks.md#humans-update

namespace coney::ai {

/// One AI human and what drawing it needs.
struct AiHuman {
    std::unique_ptr<human::Human> human;
    human::TargetSnapshot previous; ///< The last step but one.
    human::TargetSnapshot current;  ///< The last step.
};

/// The kind of the sandbox fighters' gang: the level scripts' combat gangs' kind (`GangCreate(19, ...)` in
/// `level99_combat.lua`, docs/research/ai.md#level99).
inline constexpr int kFighterGangKind = 19;

/// The AI humans of a scene.
class AiHumans {
  public:
    /// AI humans in `player`'s characters' step (both must outlive this), playing `character`'s clips (**Coney
    /// choice**: Coney loads one character, so a fighter looks and moves as the player's character does), fighting by
    /// `config`. Installs the brains in the player's step and gives the player a type-0 brain in a gang of the
    /// Warriors' kind; the fighters join a gang of kind kFighterGangKind, its enemy. Who fights whom in the step
    /// follows the gangs: humans whose gangs are not friends (ai::Gangs::friends()).
    AiHumans(human::Player& player, const human::PlayerCharacter& character, AiConfig config);
    AiHumans(const AiHumans&) = delete;
    AiHumans& operator=(const AiHumans&) = delete;
    AiHumans(AiHumans&&) = delete;
    AiHumans& operator=(AiHumans&&) = delete;
    /// The AI humans of a level whose scripts drive them, in `player`'s characters' step: the brains are `brains`
    /// (the level's, whose gangs the scripts make; it must outlive this) and the player's brain is added to them in no
    /// gang (the scripts' `HuCreate` puts him in one). There is no stand-in gang and engaging() is off: the scripts'
    /// `GoalFight` starts the fights. Who fights whom follows the gangs, as above.
    AiHumans(human::Player& player, const human::PlayerCharacter& character, AiConfig config, Brains& brains);
    /// Takes its humans out of the player's step and its brains (the player's too) out of the brains.
    ~AiHumans();

    /// Spawns a fighter (AiConfig::fighter, of the power class AiConfig::powerClass) with its feet at `feet` on `mesh`
    /// (may be null) facing `headingDegrees`; with engaging() on it fights the player once he comes within its far
    /// melee range. Returns its human.
    human::Human& spawnFighter(const raycast::CollisionMesh* mesh, anim::Vec3 feet, float headingDegrees);
    /// Spawns an AI human playing `character` (which must outlive it), of the class and power class `config` gives,
    /// fighting by its settings, with its feet at `feet` on `mesh` (may be null: no snap) facing `headingDegrees`, in
    /// no gang. Returns its brain.
    Brain& spawn(const human::PlayerCharacter& character, const AiConfig& config, const raycast::CollisionMesh* mesh,
                 anim::Vec3 feet, float headingDegrees);
    /// Removes every AI human.
    void clear();
    /// Makes `fighter`'s brain fight the player at once (`GoalFight(fighter, player, 0)`); nothing for a human that is
    /// not one of these.
    void fightPlayer(const human::Human& fighter);

    /// Whether an idle fighter starts a fight with the player when he is within its far melee range: Coney's stand-in
    /// for the level scripts' `GoalFight` calls (`level99_combat.lua`, docs/research/ai.md#level99), which a scene
    /// without its script needs. On by default.
    [[nodiscard]] bool engaging() const { return m_engaging; }
    void setEngaging(bool on) { m_engaging = on; }

    /// Keeps each human's snapshot of this step (after the player's update), for drawing.
    void capture();

    /// The AI humans spawned, their brains and the player's brain.
    [[nodiscard]] std::size_t count() const { return m_humans.size(); }
    [[nodiscard]] const std::vector<AiHuman>& humans() const { return m_humans; }
    [[nodiscard]] Brains& brains() { return *m_brains; }
    [[nodiscard]] const Brains& brains() const { return *m_brains; }
    [[nodiscard]] Brain& playerBrain() { return *m_playerBrain; }
    /// `human`'s brain (null for a human that is not one of these, or the player's).
    [[nodiscard]] Brain* brainOf(const human::Human& human) { return m_brains->find(human); }
    [[nodiscard]] const AiConfig& config() const { return m_config; }
    /// The player's gang and the fighters' (ids in brains().gangs()); -1 until a level's scripts make them.
    [[nodiscard]] int playerGang() const { return m_playerGang; }
    [[nodiscard]] int fighterGang() const { return m_fighterGang; }

  private:
    // The player's brain in the brains, and the hooks into his step: what both constructors share.
    void install();
    // The fighters' gang, made the first time a fighter is spawned in a scripted level (the enemy of the player's).
    int fighterGang();
    // The brains' place in the step: the engaging stand-in, the brains, then the player's nearest enemy.
    void step();
    // The snapshot of `human` now.
    [[nodiscard]] static human::TargetSnapshot snapshotOf(const human::Human& human);

    human::Player& m_player;
    const human::PlayerCharacter& m_character;
    AiConfig m_config;
    std::unique_ptr<Brains> m_ownBrains; // its own, without a level's scripts
    Brains* m_brains;
    Brain* m_playerBrain = nullptr;
    std::vector<AiHuman> m_humans;
    bool m_engaging = true;
    int m_playerGang = -1;
    int m_fighterGang = -1;
};

} // namespace coney::ai
