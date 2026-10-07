// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's part of the level's glass panes and doors (docs/research/objects.md#coneys-implementation): their
// world, their ticks, player 1's hits on them and the lock pick (docs/research/crimes.md#lockpick).
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "characters/character_class.h"
#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/player_combat.h"
#include "combat/stick_games.h"
#include "human/fighter.h"
#include "human/fighter_clips.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "platform/play_level_mode.h"
#include "raycast/collision_mesh.h"
#include "scripting/object_bindings.h"

namespace coney::platform {

namespace {

// **Coney's stand-in** for where a strike meets a pane or a door: a ray this high above the feet along the facing, as
// long as the attack's reach. The original strikes the object its target picker chose (`Player_ObjectAttack`,
// docs/research/combat.md#breakables), which Coney's picker does not offer yet.
constexpr float kStrikeHeight = 1.0F;
// A dropped object lands this far ahead of the feet (Coney's stand-in for its fall from the hand).
constexpr float kDropAhead = 0.3F;
// A car stereo's context record (kind 3) reaches this far in the ground plane (`CfgActionDistance`'s default).
constexpr float kStereoReach = 2.0F;
// The player's Warrior class byte `+0x0b`, which makes a theft's stage 3 turns (docs/research/combat.md#stereo-theft).
constexpr std::uint8_t kPlayerTheftByte = 2;
// **Coney's stand-in** for the reach of the pickable door's kind-2 record (not traced): triangle within this many
// metres of the door's position starts a pick.
constexpr float kLockPickReach = 1.5F;
// The record bits a run attack, a charge or a dive holds, which make a human's hit kind 2 (docs/research/objects.md).
constexpr std::uint32_t kRunAttackOrCharge = 0x1400000;
// The buttons that leave a lock pick running: cross presses, L1, R2, the d-pad and SELECT do nothing to it
// (docs/research/crimes.md#lockpick); any other press abandons it.
constexpr std::uint16_t kLockPickKeeps =
    pad::kCross | pad::kL1 | pad::kR2 | pad::kUp | pad::kDown | pad::kLeft | pad::kRight | pad::kSelect;
// The 60 Hz ticks the objects take in one 1/30 s step.
constexpr int kObjectTicksPerStep = 2;

} // namespace

void PlayLevelMode::bindObjects(world_objects::LevelObjects* objects, const script::RecordedCalls* recorded) {
    m_objects = objects;
    if (m_objects == nullptr) {
        return;
    }
    m_objects->world.collision = m_scenery->objectCollision();
    m_objects->world.paths = m_scenery->objectPaths();
    m_lockPickDifficulty = script::lockPickDifficulty(recorded, characters::warriorClassOf(m_type));
    m_print(std::format("objects: {} glass panes, {} doors and barriers\n", m_objects->glass.panes().size(),
                        m_objects->doors.doors().size()));
}

void PlayLevelMode::bindPickups(LevelPickups* pickups) {
    m_pickups = pickups;
    if (m_pickups == nullptr) {
        return;
    }
    m_player->human().setContextAction([this](human::Human& human) {
        // A ray to the object blocked by the level's collision (the panes' and doors' among it).
        const world_objects::SightBlocked blocked = [this](anim::Vec3 from, anim::Vec3 to) {
            const raycast::Vec3 d{to.x - from.x, to.y - from.y, to.z - from.z};
            const float length = std::sqrt((d.x * d.x) + (d.y * d.y) + (d.z * d.z));
            if (length < 1e-4F) {
                return false;
            }
            const raycast::Ray ray{.origin = {from.x, from.y, from.z},
                                   .direction = {d.x / length, d.y / length, d.z / length},
                                   .length = length};
            const raycast::CollisionMesh* mesh = m_objects != nullptr && m_objects->world.collision != nullptr
                                                     ? m_objects->world.collision
                                                     : &m_scenery->collision();
            return mesh->rayCast(ray, {}, 0).has_value();
        };
        human::ScriptState& script = human.script();
        const anim::Vec3 feet = human.position();
        // A car's freed stereo (a kind-3 record) comes after an object's prompt (kind 1).
        if (!m_pickups->actionObject(feet).has_value()) {
            if (const world_objects::Car* car = stereoInReach(feet)) {
                human.startStereoTheft(world_objects::Cars::stereoPosition(*car),
                                       combat::stereoStageTurns(kPlayerTheftByte));
                m_theftCar = car->handle;
                m_print(std::format("theft: the stereo of car {:.0f}\n", car->handle));
                return true;
            }
        }
        const TriangleOutcome outcome = m_pickups->triangle(playerHandle(), feet, human::facing(human.heading()),
                                                            script.heldObject != world_objects::kNoObject, blocked);
        switch (outcome.result) {
        case TriangleResult::Nothing:
            return false;
        case TriangleResult::Consumed:
            m_print("pickup: the object's handler took the press\n");
            return true;
        case TriangleResult::Drop: {
            // At once, with no clip: it lands a little ahead of the feet (Coney's stand-in for its fall).
            const anim::Vec3 ahead = human::facing(human.heading());
            const double held = std::exchange(script.heldObject, world_objects::kNoObject);
            script.heldObjectName.clear();
            m_pickups->drop(held, anim::Vec3{feet.x + (ahead.x * kDropAhead), feet.y + (ahead.y * kDropAhead), feet.z});
            m_print(std::format("pickup: dropped object {:.0f}\n", held));
            return true;
        }
        case TriangleResult::PickUp:
            break;
        }
        const PickupChoice& choice = outcome.choice;
        if (!human.startPickUp(choice.handle, choice.position, static_cast<std::uint32_t>(choice.clip))) {
            return false;
        }
        m_print(std::format("pickup: object {:.0f} with clip {}\n", choice.handle, choice.clip));
        return true;
    });
}

const world_objects::Car* PlayLevelMode::stereoInReach(anim::Vec3 feet) const {
    if (m_cars == nullptr) {
        return nullptr;
    }
    const world_objects::Car* best = nullptr;
    float bestDistance = kStereoReach;
    for (const world_objects::Car& car : m_cars->all()) {
        if (car.stereo != world_objects::StereoState::Freed) {
            continue;
        }
        const anim::Vec3 at = world_objects::Cars::stereoPosition(car);
        const float distance = std::hypot(at.x - feet.x, at.y - feet.y);
        if (distance <= bestDistance && at.z >= feet.z &&
            std::fabs(at.z - (feet.z + 1.0F)) <= LevelPickups::kPromptHeight) {
            best = &car;
            bestDistance = distance;
        }
    }
    return best;
}

void PlayLevelMode::giveObjectTargets() {
    if (m_objects == nullptr) {
        return;
    }
    std::vector<human::ObjectTarget> objects;
    for (const world_objects::GlassPane& pane : m_objects->glass.panes()) {
        if (!pane.broken && !pane.hidden) {
            objects.push_back(human::ObjectTarget{.handle = pane.handle, .point = pane.centre});
        }
    }
    // The car pass (before the objects in the original): a car the player is close to and faces offers its aim point,
    // named by the car's handle (docs/research/cars.md#windows).
    if (m_cars != nullptr) {
        const human::Human& human = m_player->human();
        const anim::Vec3 feet = human.position();
        const anim::Vec3 forward = human::facing(human.heading());
        for (const world_objects::Car& car : m_cars->all()) {
            if (!world_objects::carTargetable(car, feet, human.heading())) {
                continue;
            }
            if (const std::optional<anim::Vec3> aim = world_objects::carAimPoint(car, feet, forward)) {
                objects.push_back(human::ObjectTarget{.handle = car.handle, .point = *aim});
            }
        }
    }
    m_player->human().setObjectTargets(std::move(objects));
}

void PlayLevelMode::stepPickups() {
    if (m_pickups == nullptr) {
        return;
    }
    human::Human& human = m_player->human();
    human::ScriptState& script = human.script();
    // The stereo theft's outcome: the car's stereo is taken and paid for, or the theft is over.
    if (m_theftCar && human.fighter().last().game != combat::GameResult::Running) {
        if (human.fighter().last().game == combat::GameResult::Succeeded && m_cars != nullptr &&
            m_cars->takeStereo(*m_theftCar)) {
            m_pickups->stereoStolen(0, playerHandle(), *m_theftCar);
            m_print(std::format("theft: stole the stereo of car {:.0f}\n", *m_theftCar));
        } else {
            m_print("theft: failed\n");
        }
        m_theftCar.reset();
    }
    stepMugging(human);
    if (const std::optional<double> taken = human.takePickedUp()) {
        const TakeResult result = m_pickups->take(*taken, 0);
        if (result == TakeResult::InHand) {
            script.heldObject = *taken;
            script.heldObjectName = m_pickups->typeOf(*taken);
        }
        m_print(std::format("pickup: took object {:.0f}{}\n", *taken,
                            result == TakeResult::Gone     ? " (gone)"
                            : result == TakeResult::InHand ? " in hand"
                                                           : ""));
    }
    // An object let go some other way (HuDropWeapon, ObjDestroy) lands at the feet; the anim set follows the hand.
    if (m_heldObject != world_objects::kNoObject && m_heldObject != script.heldObject) {
        m_pickups->drop(m_heldObject, human.position());
    }
    m_heldObject = script.heldObject;
    human.fighter().setAnimSet(m_pickups->animSetOf(script.heldObjectName));
}

void PlayLevelMode::stepMugging(human::Human& human) {
    // The next mugging runs with SetInterrogateParam's record while the scripts have one set.
    combat::PlayerCombat& fight = human.fighter().combat();
    fight.setMuggingOverride(m_pickups->muggingOverride());
    // A decided mugging's callback waits for the mugger's end clip (344 or 346) to finish.
    if (m_mugEnding) {
        if (human.animator().animId() != m_mugEndClip) {
            m_pickups->mugEnded(playerHandle(), human.script().mugCallback, *m_mugEnding);
            m_print(std::format("mugging: callback ({})\n", *m_mugEnding ? "success" : "failure"));
            m_mugEnding.reset();
        }
        return;
    }
    const bool mugging = fight.mode() == combat::CombatMode::Mugging;
    const bool wasMugging = std::exchange(m_wasMugging, mugging);
    if (!wasMugging || mugging) {
        return;
    }
    // The mugging ended this step. A win pays at once, from the victim still held; a win or a loss then waits for its
    // end clip; a let-go or a hit calls back at once.
    const combat::GameResult result = human.fighter().last().game;
    if (result == combat::GameResult::Succeeded) {
        auto* victim = dynamic_cast<human::Human*>(human.fighter().held());
        int none = 0;
        int& money = victim != nullptr ? victim->script().money : none;
        const int taken = money;
        m_pickups->mugPaid(0, money);
        m_print(std::format("mugging: succeeded, ${} taken\n", taken));
    }
    if (result == combat::GameResult::Running) {
        m_pickups->mugEnded(playerHandle(), human.script().mugCallback, false);
        m_print("mugging: broken off\n");
        return;
    }
    m_mugEnding = result == combat::GameResult::Succeeded;
    m_mugEndClip = *m_mugEnding ? human::clips::kMugEnd : human::clips::kMugFail;
}

double PlayLevelMode::playerHandle() const {
    const HumanCreation* player = m_cast.humans != nullptr ? m_cast.humans->player(1) : nullptr;
    return player != nullptr ? player->handle : world_objects::kNoObject;
}

bool PlayLevelMode::stepLockPick(const Pad& pad) {
    if (m_objects == nullptr) {
        return false;
    }
    const std::uint16_t pressed = pad.pressed();
    if (!m_lockPick) {
        // Triangle at a pickable door starts a pick: the nearest within reach.
        if ((pressed & pad::kTriangle) == 0) {
            return false;
        }
        const anim::Vec3 feet = m_player->human().position();
        const world_objects::Door* nearest = nullptr;
        float nearestDistance = kLockPickReach;
        for (const world_objects::Door& door : m_objects->doors.doors()) {
            const float distance = std::hypot(door.position.x - feet.x, door.position.y - feet.y);
            if (door.pickable && !door.ended && distance <= nearestDistance) {
                nearest = &door;
                nearestDistance = distance;
            }
        }
        if (nearest == nullptr) {
            return false;
        }
        m_lockPick.emplace(m_objects->lockPick, playerHandle(), feet, nearest->handle, m_lockPickDifficulty,
                           m_objects->world);
        m_print(
            std::format("lock pick: started at door {:.0f}, difficulty {}\n", nearest->handle, m_lockPickDifficulty));
        return true;
    }
    // A pick under way: the dial turns, cross judges the current pin, another button abandons.
    m_lockPick->step();
    if ((pressed & pad::kCross) != 0) {
        m_lockPick->press(m_objects->doors, m_objects->world);
    } else if ((pressed & ~kLockPickKeeps) != 0) {
        m_lockPick->abandon(m_objects->doors, m_objects->world);
    }
    if (m_lockPick->state() != world_objects::LockPickState::Running) {
        m_print(std::format("lock pick: {}\n", m_lockPick->state() == world_objects::LockPickState::Succeeded
                                                   ? (m_lockPick->dial().perfect() ? "picked, perfect" : "picked")
                                                   : "abandoned"));
        m_lockPick.reset();
    }
    return true;
}

void PlayLevelMode::stepObjects() {
    if (m_objects == nullptr) {
        return;
    }
    // Player 1's object attack strikes its object; any other hit that landed this step goes to the pane or door the
    // strike meets, if any.
    const human::Human& human = m_player->human();
    if (const std::optional<double> attacked = human.fighter().objectHit()) {
        const anim::Vec3 feet = human.position();
        const anim::Vec3 ahead = human::facing(human.heading());
        // A car takes the hit itself (Strike_Contact calls its hit handler; no message 1), by where the player stands.
        if (m_cars != nullptr && m_cars->find(*attacked) != nullptr) {
            const world_objects::CarPartMask struck = m_cars->humanHit(*attacked, feet);
            m_print(std::format("objects: car {:.0f} hit, parts {:#x}\n", *attacked, struck));
        } else {
            const world_objects::GlassPane* pane = m_objects->glass.find(*attacked);
            const bool took = m_objects->humanHit(
                *attacked, world_objects::ObjectHit{.attacker = playerHandle(),
                                                    .kind = world_objects::humanHitKind(false, false),
                                                    .point = pane != nullptr ? pane->centre : feet,
                                                    .direction = ahead,
                                                    .attackerAt = feet});
            m_print(std::format("objects: object attack on {:.0f}{}\n", *attacked, took ? "" : " (no effect)"));
        }
    } else if (const int animId = human.fighter().last().hitAnim;
               animId != combat::anim_id::kNone && m_objects->world.collision != nullptr) {
        // The attack's reach as the fighter measures it: its far range, else the default reach.
        float reach = human::kDefaultStrikeReach;
        if (const combat::AnimRangeList* ranges = human.ranges(); ranges != nullptr && animId >= 0) {
            if (const float far = ranges->farRange(static_cast<std::size_t>(animId)); far > 0.0F) {
                reach = far;
            }
        }
        const anim::Vec3 feet = human.position();
        const anim::Vec3 ahead = human::facing(human.heading());
        const raycast::Ray ray{.origin = {feet.x, feet.y, feet.z + kStrikeHeight},
                               .direction = {ahead.x, ahead.y, ahead.z},
                               .length = reach};
        if (const std::optional<raycast::RayHit> struck = m_objects->world.collision->rayCast(ray, {}, 0)) {
            if (const std::optional<double> object = m_objects->objectOfTriangle(struck->triangle)) {
                const human::GateInput gate = human.gateInput();
                const anim::Vec3 point{feet.x + (ahead.x * struck->t), feet.y + (ahead.y * struck->t),
                                       feet.z + kStrikeHeight};
                const bool took = m_objects->humanHit(
                    *object, world_objects::ObjectHit{.attacker = playerHandle(),
                                                      .kind = world_objects::humanHitKind(
                                                          (gate.flags & kRunAttackOrCharge) != 0, gate.airborne),
                                                      .point = point,
                                                      .direction = ahead,
                                                      .attackerAt = feet});
                m_print(
                    std::format("objects: hit {:.0f} with clip {}{}\n", *object, animId, took ? "" : " (no effect)"));
            }
        }
    }
    for (int tick = 0; tick < kObjectTicksPerStep; ++tick) {
        m_objects->tick();
    }
}

} // namespace coney::platform
