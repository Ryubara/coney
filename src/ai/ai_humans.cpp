// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/ai_humans.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

#include "animation/anim_pose.h"

namespace coney::ai {

AiHumans::AiHumans(human::Player& player, const human::PlayerCharacter& character, AiConfig config)
    : m_player(player), m_character(character), m_config(std::move(config)), m_ownBrains(std::make_unique<Brains>()),
      m_brains(m_ownBrains.get()), m_sides(m_brains->gangs()) {
    install();
    // The player's gang; the fighters' gangs are made at their spawn, as enemies of it, his allies or neither, and who
    // fights whom in the step follows the gangs.
    m_brains->gangs().addMember(m_brains->gangs().create(kWarriorsKind, "Warriors"), *m_playerBrain);
}

AiHumans::AiHumans(human::Player& player, const human::PlayerCharacter& character, AiConfig config, Brains& brains)
    : m_player(player), m_character(character), m_config(std::move(config)), m_brains(&brains), m_engaging(false),
      m_sides(brains.gangs()) {
    install();
}

void AiHumans::install() {
    // The player's brain is the first slot, as the player is the first human.
    m_playerBrain = &m_brains->add(m_player.body(), BrainType::Player, m_config.settings, 1);
    givePad(*m_playerBrain);
    m_player.humans().setOpposition([this](const human::Human& attacker, const human::Human& victim) {
        const Brain* a = m_brains->find(attacker);
        const Brain* b = m_brains->find(victim);
        return !Gangs::friends(a != nullptr ? a->gang() : nullptr, b != nullptr ? b->gang() : nullptr);
    });
    m_player.humans().setBrains([this](std::span<human::Human* const> /*humans*/) { step(); });
}

void AiHumans::givePad(Brain& brain) {
    // A dead player brain takes the pad away (`GangBrDead` on the Warriors).
    brain.setPadControl([this](bool padControlled) { m_player.setPadControlled(padControlled); });
}

void AiHumans::switchPlayer(Brain& to, BrainType leftAs) {
    if (&to == m_playerBrain) {
        return;
    }
    // The human left behind fights as `leftAs`, with the flags Human_ReleasePlayer gives that brain type.
    m_playerBrain->setType(leftAs);
    human::Human& left = m_playerBrain->human();
    const std::uint64_t released = releasedFlags(left.flags(), leftAs);
    left.setFlag(~released, false);
    left.setFlag(released, true);
    to.setType(BrainType::Player);
    givePad(to);
    m_playerBrain = &to;
    m_player.drive(to.human());
    // A brain set dead before the hand-over keeps the pad away from its new human too.
    m_player.setPadControlled(!to.dead());
}

bool AiHumans::releasePlayer(BrainType leftAs) {
    if (m_playerBrain->type() != BrainType::Player) {
        return false;
    }
    releaseToAi(*m_playerBrain, leftAs);
    // The pad leaves him: from the next update only his brain writes his record.
    m_player.setPadControlled(false);
    // Back into his own gang as an AI member (Gang_AddMember): no longer its war chief, and its tactic hears of him.
    if (const Gang* gang = m_playerBrain->gang(); gang != nullptr) {
        m_brains->gangs().addMember(gang->id(), *m_playerBrain);
    }
    m_playerReleased = true;
    return true;
}

AiHumans::~AiHumans() {
    // The pad back on the player's own human before the AI humans go.
    m_player.drive(m_player.body());
    clear();
    // Brains that outlive this forget the player too.
    if (!m_ownBrains) {
        m_brains->remove(m_player.body());
    }
    m_player.humans().setBrains({});
    m_player.humans().setOpposition({});
    m_player.setPadControlled(true);
    m_player.setNearestEnemy(std::nullopt);
}

Brain& AiHumans::spawn(const human::PlayerCharacter& character, const AiConfig& config,
                       const raycast::CollisionMesh* mesh, anim::Vec3 feet, float headingDegrees) {
    const FighterClass& kind = config.fighter;
    AiHuman entry;
    entry.human =
        std::make_unique<human::Human>(character.anims(), human::AnimSlots::player(), anim::referenceRotations(),
                                       human::kPlayerBodyScale, &character.ranges(), kind.damage, 0);
    human::Human& made = *entry.human;
    made.setSkeleton(&character.skeleton());
    made.setFighterProfile(human::FighterProfile{.player = false,
                                                 .powerClass = config.powerClass,
                                                 .health = kind.health,
                                                 .bossClass = kind.category == human::kBossCategory});
    made.spawn(mesh, feet, headingDegrees);
    entry.current = snapshotOf(made);
    entry.previous = entry.current;
    // Its brain, seeded by its slot so every run rolls the same.
    Brain& brain = m_brains->add(made, kind.brain, config.settings, static_cast<std::uint32_t>(m_brains->size() + 1));
    m_player.humans().add(made, false);
    m_humans.push_back(std::move(entry));
    return brain;
}

// The scene's character on `side`.
// @coney-only debug tooling: the original has no spawner
human::Human& AiHumans::spawnFighter(const raycast::CollisionMesh* mesh, anim::Vec3 feet, float headingDegrees,
                                     FighterSide side) {
    return spawnFighter(m_character, mesh, feet, headingDegrees, side);
}

// A fighter as `character`, put in a gang of its side.
// @coney-only debug tooling: the original has no spawner
human::Human& AiHumans::spawnFighter(const human::PlayerCharacter& character, const raycast::CollisionMesh* mesh,
                                     anim::Vec3 feet, float headingDegrees, FighterSide side) {
    Brain& brain = spawn(character, m_config, mesh, feet, headingDegrees);
    m_humans.back().side = side;
    m_sides.join(side, brain, *m_playerBrain);
    return brain.human();
}

// Only an enemy of the player's gang is sent at him by the engaging stand-in.
// @coney-only the engaging stand-in is Coney's own (a level script's GoalFight does this in the original)
bool AiHumans::hostileToPlayer(const Brain& brain) const {
    const Gang* mine = brain.gang();
    const Gang* players = m_playerBrain->gang();
    return mine == nullptr || players == nullptr || Gangs::enemies(mine, players);
}

void AiHumans::remove(const human::Human& human) {
    if (std::ranges::find(m_removing, &human) == m_removing.end()) {
        m_removing.push_back(&human);
    }
}

void AiHumans::takeOutRemoved() {
    for (const human::Human* human : m_removing) {
        const auto found =
            std::ranges::find_if(m_humans, [human](const AiHuman& entry) { return entry.human.get() == human; });
        if (found == m_humans.end() || found->removed) {
            continue;
        }
        m_player.humans().remove(*human);
        m_brains->remove(*human);
        found->removed = true;
    }
    m_removing.clear();
}

void AiHumans::clear() {
    for (const AiHuman& entry : m_humans) {
        if (entry.removed) {
            continue;
        }
        m_player.humans().remove(*entry.human);
        m_brains->remove(*entry.human);
    }
    m_humans.clear();
}

void AiHumans::markDebugSpawned(const human::Human& human) {
    for (AiHuman& entry : m_humans) {
        if (entry.human.get() == &human) {
            entry.debugSpawned = true;
        }
    }
}

std::size_t AiHumans::removeDebugSpawned() {
    // The human the pad drives is never taken, whoever made it.
    const human::Human* driven = &m_playerBrain->human();
    std::size_t taken = 0;
    for (const AiHuman& entry : m_humans) {
        if (entry.debugSpawned && !entry.removed && entry.human.get() != driven) {
            remove(*entry.human);
            ++taken;
        }
    }
    // Out of the step and the brains now, as capture() does it for the others.
    takeOutRemoved();
    return taken;
}

std::size_t AiHumans::liveCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_humans, [this](const AiHuman& entry) {
        return !entry.removed && std::ranges::find(m_removing, entry.human.get()) == m_removing.end();
    }));
}

void AiHumans::fightPlayer(const human::Human& fighter) {
    if (Brain* brain = m_brains->find(fighter); brain != nullptr && brain != m_playerBrain) {
        brain->startFight(*m_playerBrain);
    }
}

void AiHumans::step() {
    // The stand-in for the level script's GoalFight: an idle fighter takes the player on once he is in range.
    if (m_engaging && Brain::fightable(*m_playerBrain)) {
        for (const AiHuman& entry : m_humans) {
            Brain* brain = m_brains->find(*entry.human);
            if (brain != nullptr && Brain::fightable(*brain) && brain->goalCount() == 0 &&
                brain->reactionGoal() == nullptr && hostileToPlayer(*brain) &&
                brain->distanceTo(*m_playerBrain) <= brain->meleeFar()) {
                brain->startFight(*m_playerBrain);
            }
        }
    }
    m_brains->update();
    m_player.setNearestEnemy(Brains::nearestEnemy(*m_playerBrain));
}

void AiHumans::capture() {
    // The humans deleted during the step leave it now, outside the characters' step.
    takeOutRemoved();
    for (AiHuman& entry : m_humans) {
        entry.previous = entry.current;
        entry.current = snapshotOf(*entry.human);
    }
}

human::TargetSnapshot AiHumans::snapshotOf(const human::Human& human) {
    return human::TargetSnapshot{.feet = human.position(), .heading = human.heading(), .pose = human.pose()};
}

} // namespace coney::ai
