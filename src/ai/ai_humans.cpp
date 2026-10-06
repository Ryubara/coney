// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/ai_humans.h"

#include <span>
#include <utility>

#include "animation/anim_pose.h"

namespace coney::ai {

AiHumans::AiHumans(human::Player& player, const human::PlayerCharacter& character, AiConfig config)
    : m_player(player), m_character(character), m_config(std::move(config)) {
    // The player's brain is the first slot, as the player is the first human.
    m_playerBrain = &m_brains.add(player.human(), BrainType::Player, m_config.settings, 1);
    // A dead player brain takes the pad away (`GangBrDead` on the Warriors).
    m_playerBrain->setPadControl([this](bool padControlled) { m_player.setPadControlled(padControlled); });
    // The player's gang and the fighters', enemies of each other; who fights whom in the step follows the gangs.
    m_playerGang = m_brains.gangs().create(kWarriorsKind, "Warriors");
    m_fighterGang = m_brains.gangs().create(kFighterGangKind, "Fighters");
    m_brains.gangs().makeEnemies(m_playerGang, m_fighterGang);
    m_brains.gangs().addMember(m_playerGang, *m_playerBrain);
    m_player.humans().setOpposition([this](const human::Human& attacker, const human::Human& victim) {
        const Brain* a = m_brains.find(attacker);
        const Brain* b = m_brains.find(victim);
        return !Gangs::friends(a != nullptr ? a->gang() : nullptr, b != nullptr ? b->gang() : nullptr);
    });
    m_player.humans().setBrains([this](std::span<human::Human* const> /*humans*/) { step(); });
}

AiHumans::~AiHumans() {
    clear();
    m_player.humans().setBrains({});
    m_player.humans().setOpposition({});
    m_player.setPadControlled(true);
    m_player.setNearestEnemy(std::nullopt);
}

human::Human& AiHumans::spawnFighter(const raycast::CollisionMesh* mesh, anim::Vec3 feet, float headingDegrees) {
    const FighterClass& kind = m_config.fighter;
    AiHuman entry;
    entry.human =
        std::make_unique<human::Human>(m_character.anims(), human::AnimSlots::player(), anim::referenceRotations(),
                                       human::kPlayerBodyScale, &m_character.ranges(), kind.damage, 0);
    human::Human& made = *entry.human;
    made.setFighterProfile(
        human::FighterProfile{.player = false, .powerClass = m_config.powerClass, .health = kind.health});
    made.spawn(mesh, feet, headingDegrees);
    entry.current = snapshotOf(made);
    entry.previous = entry.current;
    // Its brain, seeded by its slot so every run rolls the same.
    Brain& brain = m_brains.add(made, kind.brain, m_config.settings, static_cast<std::uint32_t>(m_brains.size() + 1));
    m_brains.gangs().addMember(m_fighterGang, brain);
    m_player.humans().add(made, false);
    m_humans.push_back(std::move(entry));
    return made;
}

void AiHumans::clear() {
    for (const AiHuman& entry : m_humans) {
        m_player.humans().remove(*entry.human);
        m_brains.remove(*entry.human);
    }
    m_humans.clear();
}

void AiHumans::fightPlayer(const human::Human& fighter) {
    if (Brain* brain = m_brains.find(fighter); brain != nullptr && brain != m_playerBrain) {
        brain->startFight(*m_playerBrain);
    }
}

void AiHumans::step() {
    // The stand-in for the level script's GoalFight: an idle fighter takes the player on once he is in range.
    if (m_engaging && Brain::fightable(*m_playerBrain)) {
        for (const AiHuman& entry : m_humans) {
            Brain* brain = m_brains.find(*entry.human);
            if (brain != nullptr && Brain::fightable(*brain) && brain->goalCount() == 0 &&
                brain->reactionGoal() == nullptr && brain->distanceTo(*m_playerBrain) <= brain->meleeFar()) {
                brain->startFight(*m_playerBrain);
            }
        }
    }
    m_brains.update();
    m_player.setNearestEnemy(Brains::nearestEnemy(*m_playerBrain));
}

void AiHumans::capture() {
    for (AiHuman& entry : m_humans) {
        entry.previous = entry.current;
        entry.current = snapshotOf(*entry.human);
    }
}

human::TargetSnapshot AiHumans::snapshotOf(const human::Human& human) {
    return human::TargetSnapshot{.feet = human.position(), .heading = human.heading(), .pose = human.pose()};
}

} // namespace coney::ai
