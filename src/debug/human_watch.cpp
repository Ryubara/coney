// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/human_watch.h"

#include <array>
#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <utility>

#include "ai/gangs.h"
#include "animation/anim_task.h"
#include "human/body.h"
#include "human/human_flags.h"

namespace coney::debug {

namespace {

// Radians as degrees, for headings and angles.
float degrees(float radians) { return radians * 180.0F / std::numbers::pi_v<float>; }

// "yes" or "no".
std::string_view yesNo(bool on) { return on ? "yes" : "no"; }

// A group being filled: each add() appends a line.
class GroupBuilder {
  public:
    explicit GroupBuilder(std::string name) { m_group.name = std::move(name); }
    // Appends the line `name: value`.
    GroupBuilder& add(std::string name, std::string value) {
        m_group.lines.push_back(WatchLine{.name = std::move(name), .value = std::move(value)});
        return *this;
    }
    // The group made.
    WatchGroup take() { return std::move(m_group); }

  private:
    WatchGroup m_group;
};

// A target state's short name.
std::string_view targetStateName(human::TargetState state) {
    switch (state) {
    case human::TargetState::Standing:
        return "standing";
    case human::TargetState::Held:
        return "held";
    case human::TargetState::Mounted:
        return "mounted";
    case human::TargetState::Grounded:
        return "grounded";
    }
    return "?";
}

// A combat mode's short name.
std::string_view combatModeName(combat::CombatMode mode) {
    switch (mode) {
    case combat::CombatMode::Free:
        return "free";
    case combat::CombatMode::Grabbing:
        return "grabbing";
    case combat::CombatMode::Tackling:
        return "tackling";
    case combat::CombatMode::Mugging:
        return "mugging";
    case combat::CombatMode::Theft:
        return "theft";
    }
    return "?";
}

// A grab pair's stage, short.
std::string_view pairStageName(human::PairStage stage) {
    switch (stage) {
    case human::PairStage::None:
        return "none";
    case human::PairStage::Intro:
        return "intro";
    case human::PairStage::Moving:
        return "moving";
    case human::PairStage::Attached:
        return "attached";
    }
    return "?";
}

// Where the human is held up: on the ground, in the air, or flying (the noclip).
std::string_view supportName(const human::Human& human) {
    if (human.flying()) {
        return "noclip";
    }
    if (human.outOfWorld()) {
        return "out of the world";
    }
    return human.airborne() ? "airborne" : "grounded";
}

// Identity: who the human is to the scene.
WatchGroup identityGroup(const human::Human& human, const HumanFacts& facts) {
    GroupBuilder group("Identity");
    group.add("Entity", std::format("{}", facts.id));
    group.add("Role", facts.role.empty() ? "-" : facts.role);
    group.add("Model", facts.model.empty() ? "-" : facts.model);
    group.add("Character type", facts.type ? std::format("{}", *facts.type) : "-");
    group.add("Controlled by", human.record().padDriven ? "pad" : (facts.brain != nullptr ? "brain" : "nothing"));
    group.add("Player profile", std::string(yesNo(human.fighterProfile().player)));
    if (facts.brain != nullptr) {
        const ai::Brain& brain = *facts.brain;
        group.add("Handle", std::format("{}", brain.handle()));
        group.add("Character class", std::format("{}", brain.characterClass()));
        const ai::Gang* gang = brain.gang();
        group.add("Gang", gang != nullptr ? std::format("{} ({}, kind {})", gang->id(), gang->name(), gang->kind())
                                          : std::string("-"));
    }
    return group.take();
}

// Transform: where it is and how it moves.
WatchGroup transformGroup(const human::Human& human) {
    GroupBuilder group("Transform");
    group.add("Feet", vecText(human.position()));
    group.add("Heading", std::format("{:.1f} deg", degrees(human.heading())));
    group.add("Velocity", vecText(human.velocity()));
    group.add("Speed", std::format("{:.2f} m/s", human.speed()));
    group.add("Scale", std::format("{:.3f}", human.scale()));
    group.add("Lean", std::format("{:.1f} deg", degrees(human.lean())));
    return group.take();
}

// Locomotion: the gait, the ground or the air, and what drives it (the record a pad or a brain writes).
WatchGroup locomotionGroup(const human::Human& human, bool frozen) {
    GroupBuilder group("Locomotion");
    const human::PlayerRecord& record = human.record();
    group.add("Gait", std::string(gaitName(human.gait())));
    group.add("Support", std::string(supportName(human)));
    group.add("Traversal", human::traversalName(human.traversal()));
    group.add("Stick", std::format("({:.2f}, {:.2f})", record.stickX, record.stickY));
    group.add("Intent", std::format("{:.2f} at {:.1f} deg", human.intent().magnitude, degrees(human.intent().angle)));
    group.add("Command", std::format("{:#x} (pad {:#x})", record.command, record.padCommand));
    group.add("Buttons", std::format("{:#06x}", record.buttons));
    group.add("Sprint held", std::string(yesNo(record.sprintHeld)));
    group.add("Brain move", record.move ? std::format("{:.2f} m/s at {:.1f} deg, aim {}", record.move->speed,
                                                      degrees(record.move->heading), vecText(record.move->aim))
                                        : std::string("-"));
    group.add("Stick held", std::string(yesNo(human.stickHeld())));
    group.add("Frozen", std::string(yesNo(frozen)));
    group.add("Ground normal", vecText(human.groundNormal()));
    group.add("Ground", std::format("material {}, flags {:#06x}{}", human.groundMaterial(), human.groundFlags(),
                                    human.onCoveredGround() ? ", covered" : ""));
    group.add("Landings", std::format("{} (last {:.2f} m/s)", human.landings(), human.lastLandingSpeed()));
    return group.take();
}

// Animation: the clip playing, its time and the record's held flags.
WatchGroup animationGroup(const human::Human& human) {
    GroupBuilder group("Animation");
    const human::HumanAnimator& animator = human.animator();
    group.add("Clip", std::format("{}", animator.animId()));
    group.add("State", std::format("{} ({})", animStateName(animator.state()), static_cast<int>(animator.state())));
    if (const anim::AnimTask* top = animator.tasks().top(); top != nullptr) {
        group.add("Time",
                  std::format("{:.3f} / {:.3f} s (frame {:.0f})", top->time(), top->duration(), top->time() * 30.0F));
        group.add("Task",
                  std::format("type {}, {} layers", static_cast<int>(top->type()), animator.tasks().layerCount()));
    } else {
        group.add("Time", "-");
    }
    group.add("Held flags (+0x08)", std::format("{:#010x}", animator.flags()));
    group.add("Gait value", std::format("{:.2f}", animator.gaitValue()));
    group.add("Idle clip", human.idleClipName().empty() ? "-" : human.idleClipName());
    const std::optional<std::uint32_t> actionEvent = human.actionEvent();
    group.add("Action event", actionEvent.has_value() ? std::format("{}", *actionEvent) : std::string("-"));
    return group.take();
}

// State flags: the human flag word decoded, and the script state's switches.
WatchGroup flagsGroup(const human::Human& human) {
    GroupBuilder group("State flags");
    group.add("Human flags", std::format("{:#x}", human.flags()));
    group.add("Named", humanFlagNames(human.flags()));
    const human::ScriptState& script = human.script();
    std::string switches;
    // Each switch the scripts can set that is on, by the field's name.
    for (const auto& [name, on] : std::array<std::pair<std::string_view, bool>, 9>{{
             {"arrested", script.arrested},
             {"knockedOut", script.knockedOut},
             {"wounded", script.wounded},
             {"padLocked", script.padLocked},
             {"movementLocked", script.movementLocked},
             {"throwAiming", script.throwAiming},
             {"unpushable", !script.pushable},
             {"untargetable", !script.targetable},
             {"hidden", human.hidden()},
         }}) {
        if (on) {
            switches += switches.empty() ? std::string(name) : " " + std::string(name);
        }
    }
    group.add("Script state", switches.empty() ? "-" : switches);
    group.add("Disabled commands", std::format("{:#x}", script.disabledCommands));
    group.add("Target state", std::string(targetStateName(human.state())));
    return group.take();
}

// Meters: health, stamina, power and rage.
WatchGroup metersGroup(const human::Human& human) {
    GroupBuilder group("Meters");
    const combat::Health& health = human.health();
    group.add("Health", std::format("{}/{} ({:.0f}%)", health.value(), health.maximum(), human.healthPercent()));
    group.add("Stamina", std::format("{}/{}{}", human.stamina().value(), human.stamina().maximum(),
                                     human.sprinting() ? ", sprinting" : ""));
    const combat::PlayerCombat& combat = human.fighter().combat();
    group.add("Power", std::format("{}/{}", combat.power().value(), combat.power().maximum()));
    group.add("Rage", std::format("{}/{}{}", combat.rage().value(), combat.rage().maximum(),
                                  combat.rage().raging() ? ", raging" : ""));
    group.add("Hurt", std::string(yesNo(human.fighter().hurt())));
    return group.take();
}

// AI: the brain's type, goal stack, actions and target.
WatchGroup aiGroup(const ai::Brain& brain) {
    GroupBuilder group("AI");
    group.add("Brain type", std::string(brainTypeName(brain.type())));
    group.add("Enabled", std::format("{}{}", yesNo(brain.enabled()), brain.suspended() ? ", suspended" : ""));
    // The goal stack from the top down, by name.
    std::string goals;
    for (std::size_t i = brain.goalCount(); i-- > 0;) {
        if (const ai::Goal* goal = brain.goalAt(i); goal != nullptr) {
            goals += std::format("{}{}", goals.empty() ? "" : " > ", goalTypeName(goal->type()));
        }
    }
    group.add("Goals (top first)", goals.empty() ? "-" : goals);
    const ai::Goal* reaction = brain.reactionGoal();
    group.add("Reaction goal", reaction != nullptr ? std::string(goalTypeName(reaction->type())) : "-");
    group.add("Actions queued", std::format("{}", brain.actionCount()));
    const ai::Brain* target = brain.target();
    group.add("Target", target != nullptr
                            ? std::format("handle {} at {:.1f} m", target->handle(),
                                          anim::distance(target->human().position(), brain.human().position()))
                            : std::string("-"));
    group.add("Enemies", std::format("{}", brain.enemies().size()));
    group.add("Attack slots", std::format("{} of {}", brain.attackSlots().size(), brain.attackSlotCount()));
    group.add("Melee range", std::format("{:.1f}-{:.1f} m", brain.meleeNear(), brain.meleeFar()));
    group.add("Move aim", vecText(brain.moveAim()));
    group.add("Down", std::format("{}{}", brain.knockedOut() ? "knocked out" : "no", brain.dead() ? ", dead" : ""));
    group.add("Thinks", std::format("{} of {} updates", brain.thinks(), brain.updates()));
    return group.take();
}

// Physics: the walking sphere and the other bodies the engine moves the human with.
WatchGroup physicsGroup(const human::Human& human) {
    GroupBuilder group("Physics");
    const human::BodyTuning& tuning = human::bodyTuning();
    const bool player = human.fighterProfile().player;
    group.add("Walking sphere", std::format("r {:.4f} m, centre {:.4f} m up", human.walkingSphereRadius(),
                                            human.walkingSphereCentreHeight()));
    group.add("Shape +0x40", std::format("{:.4f} m", human::walkingRadius(human.scale())));
    group.add("Body +0x60", std::format("{:.4f}", player ? tuning.playerFactor : 1.0F));
    const human::UprightCapsule capsule = human.uprightCapsule();
    group.add("Capsule", std::format("r {:.3f} m, {:.3f} m tall", capsule.radius, capsule.top));
    group.add("Air sphere", std::format("r {:.3f} m", human.airSphereRadius()));
    group.add("Contacts", std::format("{}", human.contacts().size()));
    group.add("Last contact", human.contacts().empty() ? std::string("-") : vecText(human.contacts().back()));
    group.add("Blocked updates", std::format("{}", human.blockedUpdates()));
    group.add("Airborne updates", std::format("{}", human.airborneUpdates()));
    const auto& climb = human.climb();
    group.add("Climb", climb.has_value() ? std::format("face {} at {:.2f} m, top {:.2f} m", vecText(climb->face),
                                                       climb->distance, climb->top)
                                         : std::string("-"));
    return group.take();
}

// Combat: the attack playing, the pair, who it fights and what it has done.
WatchGroup combatGroup(const human::Human& human) {
    GroupBuilder group("Combat");
    const human::Fighter& fighter = human.fighter();
    const combat::PlayerCombat& combat = fighter.combat();
    group.add("Mode", std::string(combatModeName(combat.mode())));
    group.add("Attack", combat.chain().active() ? std::format("anim {} (age {}, hit at {})", combat.chain().animId(),
                                                              combat.chain().age(), combat.chain().hitUpdate())
                                                : std::string("-"));
    group.add("Blocking", std::string(yesNo(fighter.blocking())));
    group.add("Pair", std::format("{}{}", pairStageName(fighter.pairStage()), fighter.inPair() ? ", in a pair" : ""));
    group.add("Holding", std::string(yesNo(fighter.held() != nullptr)));
    group.add("Grabbed", fighter.grabbed() ? (fighter.grabbedFromRear() ? "from the rear" : "from the front") : "no");
    group.add("Target", fighter.target() != nullptr ? vecText(fighter.target()->position()) : std::string("-"));
    group.add("Strike shapes on", std::format("{}", human.posedStrikeShapes(false).size()));
    group.add("Hits", std::format("landed {} ({} damage), taken {}, blocked {}", fighter.hitsLanded(),
                                  fighter.damageDealt(), fighter.hitsTaken(), fighter.hitsBlocked()));
    return group.take();
}

} // namespace

std::vector<WatchGroup> describeHuman(const human::Human& human, const HumanFacts& facts) {
    std::vector<WatchGroup> groups;
    groups.push_back(identityGroup(human, facts));
    groups.push_back(transformGroup(human));
    groups.push_back(locomotionGroup(human, facts.frozen));
    groups.push_back(animationGroup(human));
    groups.push_back(flagsGroup(human));
    groups.push_back(metersGroup(human));
    if (facts.brain != nullptr) {
        groups.push_back(aiGroup(*facts.brain));
    }
    groups.push_back(physicsGroup(human));
    groups.push_back(combatGroup(human));
    return groups;
}

std::vector<WatchGroup> describeTarget(const human::TargetHuman& target, const HumanFacts& facts) {
    std::vector<WatchGroup> groups;
    GroupBuilder identity("Identity");
    identity.add("Entity", std::format("{}", facts.id));
    identity.add("Role", facts.role.empty() ? "-" : facts.role);
    identity.add("Model", facts.model.empty() ? "-" : facts.model);
    identity.add("Controlled by", "nothing (a passive target)");
    groups.push_back(identity.take());
    GroupBuilder transform("Transform");
    transform.add("Feet", vecText(target.position()));
    transform.add("Heading", std::format("{:.1f} deg", degrees(target.heading())));
    transform.add("Attached", std::string(yesNo(target.attached())));
    groups.push_back(transform.take());
    GroupBuilder animation("Animation");
    const human::HumanAnimator& animator = target.animator();
    animation.add("Clip", std::format("{}", animator.animId()));
    animation.add("State", std::format("{} ({})", animStateName(animator.state()), static_cast<int>(animator.state())));
    animation.add("Target state", std::string(targetStateName(target.state())));
    groups.push_back(animation.take());
    GroupBuilder meters("Meters");
    const combat::Health& health = target.health();
    meters.add("Health", std::format("{}/{}", health.value(), health.maximum()));
    meters.add("Hurt", std::string(yesNo(target.hurt())));
    meters.add("Stunned", std::string(yesNo(target.stunned())));
    groups.push_back(meters.take());
    GroupBuilder combat("Combat");
    combat.add("Hits taken", std::format("{} ({} damage)", target.hitsTaken(), target.damageTaken()));
    combat.add("Reactions", std::format("{} (last clip {})", target.reactions(), target.lastReaction()));
    combat.add("Stuns", std::format("{}", target.stuns()));
    combat.add("Knockdowns", std::format("{}", target.knockdowns()));
    groups.push_back(combat.take());
    return groups;
}

std::string_view gaitName(human::Gait gait) {
    switch (gait) {
    case human::Gait::Standing:
        return "standing";
    case human::Gait::Sneak:
        return "sneak";
    case human::Gait::Walk:
        return "walk";
    case human::Gait::Jog:
        return "jog";
    case human::Gait::Run:
        return "run";
    case human::Gait::Sprint:
        return "sprint";
    }
    return "?";
}

std::string_view animStateName(human::AnimState state) {
    switch (state) {
    case human::AnimState::None:
        return "none";
    case human::AnimState::Idle:
        return "idle";
    case human::AnimState::Move:
        return "move";
    case human::AnimState::Jump:
        return "jump";
    case human::AnimState::Fall:
        return "fall";
    case human::AnimState::Land:
        return "land";
    case human::AnimState::RunStop:
        return "run stop";
    case human::AnimState::Climb:
        return "climb";
    case human::AnimState::Attack:
        return "attack";
    case human::AnimState::Hold:
        return "hold";
    case human::AnimState::CombatWalk:
        return "combat walk";
    case human::AnimState::Workout:
        return "workout";
    case human::AnimState::Debug:
        return "debug clip";
    }
    return "?";
}

std::string_view brainTypeName(ai::BrainType type) {
    switch (type) {
    case ai::BrainType::Player:
        return "player";
    case ai::BrainType::Cop:
        return "cop";
    case ai::BrainType::Gang:
        return "gang";
    case ai::BrainType::Warrior:
        return "warrior";
    case ai::BrainType::Civilian:
        return "civilian";
    case ai::BrainType::Dealer:
        return "dealer";
    case ai::BrainType::CivilianDi:
        return "civilian (di)";
    }
    return "?";
}

std::string_view goalTypeName(ai::GoalType type) {
    using enum ai::GoalType;
    switch (type) {
    case Idle:
        return "Idle";
    case MoveToFlag:
        return "MoveToFlag";
    case MoveToExitFlag:
        return "MoveToExitFlag";
    case MoveToUseFlag:
        return "MoveToUseFlag";
    case MoveToHuman:
        return "MoveToHuman";
    case Melee:
        return "Melee";
    case EngageEnemy:
        return "EngageEnemy";
    case Fight:
        return "Fight";
    case Spectate:
        return "Spectate";
    case ReactGrabbing:
        return "ReactGrabbing";
    case ReactTackling:
        return "ReactTackling";
    case ReactGrabbed:
        return "ReactGrabbed";
    case ReactTackled:
        return "ReactTackled";
    case ReactKnockedDown:
        return "ReactKnockedDown";
    case ReactStunned:
        return "ReactStunned";
    case Block:
        return "Block";
    case HelpRespond:
        return "HelpRespond";
    case GrabTarget:
        return "GrabTarget";
    case PlayAnimation:
        return "PlayAnimation";
    case PlayDynAnimation:
        return "PlayDynAnimation";
    case PlayDynIdle:
        return "PlayDynIdle";
    case PlayGenAnim:
        return "PlayGenAnim";
    case GuardFlag:
        return "GuardFlag";
    case GetItem:
        return "GetItem";
    case DestroyItem:
        return "DestroyItem";
    case TrackHuman:
        return "TrackHuman";
    case FollowPlayer:
        return "FollowPlayer";
    case FollowFormation:
        return "FollowFormation";
    case FollowAndAttack:
        return "FollowAndAttack";
    case FollowAndDefend:
        return "FollowAndDefend";
    case HoldPosition:
        return "HoldPosition";
    case TravelPath:
        return "TravelPath";
    case LeftTurf:
        return "LeftTurf";
    case FindEnemy:
        return "FindEnemy";
    case AreaWalker:
        return "AreaWalker";
    case LeadChase:
        return "LeadChase";
    case RunCarrot:
        return "RunCarrot";
    case BumLogic:
        return "BumLogic";
    case Peddler:
        return "Peddler";
    case Riot:
        return "Riot";
    case AddressPerson:
        return "AddressPerson";
    case Tag:
        return "Tag";
    case ThrowObject:
        return "ThrowObject";
    case Pedestrian:
        return "Pedestrian";
    case PedestrianReaction:
        return "PedestrianReaction";
    case CallGang:
        return "CallGang";
    case Scout:
        return "Scout";
    case Dealer:
        return "Dealer";
    case Shopkeeper:
        return "Shopkeeper";
    case WarriorVandalSteal:
        return "WarriorVandalSteal";
    case BigBrawler:
        return "BigBrawler";
    case BigThrower:
        return "BigThrower";
    case BigLedgeThrower:
        return "BigLedgeThrower";
    case StationaryThrower:
        return "StationaryThrower";
    case Tired:
        return "Tired";
    case DevilRun:
        return "DevilRun";
    case Backoff:
        return "Backoff";
    case Boxer:
        return "Boxer";
    }
    return "?";
}

std::string humanFlagNames(std::uint64_t flags) {
    namespace f = human::flag;
    // Every flag human/human_flags.h names, by its constant's name without the k.
    static constexpr std::array<std::pair<std::uint64_t, std::string_view>, 35> kNames{{
        {f::kFastClimber, "FastClimber"},
        {f::kRevivable, "Revivable"},
        {f::kWoundable, "Woundable"},
        {f::kGod, "God"},
        {f::kKiller, "Killer"},
        {f::kUngrabbable, "Ungrabbable"},
        {f::kUngroundable, "Ungroundable"},
        {f::kUnstunnable, "Unstunnable"},
        {f::kReducedReact, "ReducedReact"},
        {f::kComboStrength, "ComboStrength"},
        {f::kNoReact, "NoReact"},
        {f::kBlockLook, "BlockLook"},
        {f::kKeepWeapon, "KeepWeapon"},
        {f::kDoubleDamage, "DoubleDamage"},
        {f::kUnarrestable, "Unarrestable"},
        {f::kForceLook, "ForceLook"},
        {f::kAutoEscape, "AutoEscape"},
        {f::kRageLocked, "RageLocked"},
        {f::kIncreasedReact, "IncreasedReact"},
        {f::kRubberNeck, "RubberNeck"},
        {f::kRageAllowed, "RageAllowed"},
        {f::kTireless, "Tireless"},
        {f::kBlockJump, "BlockJump"},
        {f::kBlockFrontGrab, "BlockFrontGrab"},
        {f::kBlockRearGrab, "BlockRearGrab"},
        {f::kBlockTackle, "BlockTackle"},
        {f::kMovementLocked, "MovementLocked"},
        {f::kNoEscape, "NoEscape"},
        {f::kNoThrowWeapon, "NoThrowWeapon"},
        {f::kNoAutoLock, "NoAutoLock"},
        {f::kKeepHat, "KeepHat"},
        {f::kDemiGod, "DemiGod"},
        {f::kWheelchair, "Wheelchair"},
        {f::kNoTarget, "NoTarget"},
        {f::kAutoCombat, "AutoCombat"},
    }};
    std::string names;
    std::uint64_t unnamed = flags;
    for (const auto& [bit, name] : kNames) {
        if (bit != 0 && (flags & bit) != 0) {
            names += names.empty() ? std::string(name) : " " + std::string(name);
            unnamed &= ~bit;
        }
    }
    if (unnamed != 0) {
        names += std::format("{}{:#x}", names.empty() ? "" : " ", unnamed);
    }
    return names.empty() ? "-" : names;
}

} // namespace coney::debug
