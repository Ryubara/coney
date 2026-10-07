// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's part of the level's glass panes and doors (docs/research/objects.md#coneys-implementation): their
// world, their ticks, player 1's hits on them and the lock pick (docs/research/crimes.md#lockpick).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/dealer_goal.h"
#include "ai/goal.h"
#include "characters/anim_set.h"
#include "characters/character_class.h"
#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/attacks.h"
#include "combat/player_combat.h"
#include "combat/stick_games.h"
#include "human/fighter.h"
#include "human/fighter_clips.h"
#include "human/human.h"
#include "human/humans.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"
#include "platform/play_level_mode.h"
#include "raycast/collision_mesh.h"
#include "scripting/object_bindings.h"
#include "scripting/sound_bindings.h"
#include "warriors/inventory.h"
#include "world_objects/glass.h"

namespace coney::platform {

namespace {

// **Coney's stand-in** for where a strike meets a pane or a door: a ray this high above the feet along the facing, as
// long as the attack's reach. The original strikes the object its target picker chose (`Player_ObjectAttack`,
// docs/research/combat.md#breakables), which Coney's picker does not offer yet.
constexpr float kStrikeHeight = 1.0F;
// **Coney's stand-in** for a moving attack's strike shapes (its capsule and bone shapes posed by the clip,
// docs/research/combat.md#moving-strikes): the ray above, this long, every update of the attack's strike window. The
// lengths are where the shapes first met level99's fence: the charge's root 0.80 m from it, the dive's 1.05 m.
constexpr float kMovingStrikeReach = 0.8F;
constexpr float kDiveStrikeReach = 1.05F;
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
// The flash's clip, 665 SPECIAL_FLASH, and the record bits it holds (docs/research/combat.md#rage); its fade in is
// Coney's (the scripted clips' 0.2 s).
constexpr std::uint32_t kFlashAnim = 665;
constexpr std::uint32_t kFlashHeld = 0x2000;
constexpr float kFlashFade = 0.2F;
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
    // An airborne human breaks every pane whose body he reaches, as Strike_Contact does
    // (docs/research/objects.md#pane-break).
    m_player->humans().setBodyContact([this](human::Human& human, anim::Vec3 centre, float radius) {
        for (const double pane : m_objects->glass.bodiesTouching(centre, radius)) {
            const ai::Brain* brain = m_ai != nullptr ? m_ai->brainOf(human) : nullptr;
            const double attacker = &human == &m_player->human() ? playerHandle()
                                    : brain != nullptr           ? brain->handle()
                                                                 : world_objects::kNoObject;
            const bool took =
                m_objects->humanHit(pane, world_objects::ObjectHit{.attacker = attacker,
                                                                   .kind = world_objects::humanHitKind(false, true),
                                                                   .point = centre,
                                                                   .direction = human::facing(human.heading()),
                                                                   .attackerAt = human.position()});
            if (took) {
                m_print(std::format("objects: pane {:.0f} broken by an airborne body\n", pane));
            }
        }
    });
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
        // A dealer's offer (kind 4) after the objects' prompts and the stereos.
        if (!m_pickups->actionObject(feet).has_value() && tryDeal(human)) {
            return true;
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

bool PlayLevelMode::tryDeal(human::Human& human) {
    if (m_ai == nullptr || m_pickups == nullptr) {
        return false;
    }
    const anim::Vec3 feet = human.position();
    ai::Brains& brains = m_ai->brains();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        ai::Brain& brain = brains.at(i);
        ai::Goal* goal = brain.topGoal();
        if (goal == nullptr || goal->type() != ai::GoalType::Dealer) {
            continue;
        }
        auto& dealer = static_cast<ai::DealerGoal&>(*goal);
        const anim::Vec3 at = brain.human().position();
        if (!dealer.offering() || std::hypot(at.x - feet.x, at.y - feet.y) > ai::kDealReach ||
            std::fabs(at.z - (feet.z + 1.0F)) > LevelPickups::kPromptHeight) {
            continue;
        }
        // The deal's terms against player 1's money and item; the dealer decides, the inventory follows.
        const std::optional<ai::DealTerms> terms = ai::dealTerms(dealer.type());
        if (!terms) {
            continue;
        }
        const ai::DealOutcome outcome =
            dealer.deal(brain, m_ai->playerBrain(), m_pickups->carried(0, item::kMoney),
                        m_pickups->carried(0, terms->item), m_pickups->itemLimit(terms->item));
        if (outcome == ai::DealOutcome::Sold) {
            m_pickups->dealerSold(0, terms->item, terms->amount, terms->price);
        } else if (outcome == ai::DealOutcome::RippedOff) {
            m_pickups->dealerSold(0, terms->item, 0, terms->price);
        }
        // His line about it, said with the buyer as its target.
        if (const std::optional<std::uint32_t> line = dealer.dealLine(outcome, brain.nowMs());
            line && m_sound != nullptr) {
            static_cast<void>(m_sound->sayCommand(
                script::CommandCall{
                    .human = brain.handle(), .command = *line, .interrupt = true, .target = playerHandle()},
                {}));
        }
        m_print(std::format("deal: dealer {} outcome {}\n", i, static_cast<int>(outcome)));
        return outcome != ai::DealOutcome::NotDealing;
    }
    return false;
}

void PlayLevelMode::stepFlash() {
    human::Human& human = m_player->human();
    if (human.record().command != combat::command::kDpadRight || m_pickups == nullptr ||
        m_pickups->carried(0, item::kRevive) < 1) {
        return;
    }
    // Not while out of health, down or airborne; at full health it would only feed rage (not built).
    combat::Health& health = human.fighter().health();
    if (health.depleted() || human.airborne() || human.fighter().helpless(human.animator()) ||
        health.value() >= health.maximum()) {
        return;
    }
    // A grab he holds or is held in is let go first; then 665 when nothing holds his moves, and the flash is used.
    human.breakPair();
    if (!human::stickBusy(human.gateInput())) {
        if (const anim::AnimClip* clip = human.anims().clip(kFlashAnim)) {
            human.playScripted(*clip, kFlashAnim, 1.0F, kFlashFade, human::HeldFlags{.held = kFlashHeld});
        }
    }
    m_pickups->spendItem(0, item::kRevive);
    m_flashRingRequest = true;
    human.setWounded(false);
    health.set(health.maximum());
    m_print(std::format("flash: used, health {}\n", health.value()));
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
    // The clip's age in updates from its first (0), for a moving strike; a new clip starts with nothing struck.
    if (const std::uint32_t clip = human.animator().animId(); clip != m_strikeClip) {
        m_strikeClip = clip;
        m_strikeAge = 0;
        m_struck.clear();
    } else {
        ++m_strikeAge;
    }
    if (const std::optional<double> attacked = human.fighter().objectHit()) {
        const anim::Vec3 feet = human.position();
        const anim::Vec3 ahead = human::facing(human.heading());
        // A car takes the hit itself (Strike_Contact calls its hit handler; no message 1), by where the player stands.
        if (m_cars != nullptr && m_cars->find(*attacked) != nullptr) {
            std::vector<world_objects::CarHitReport> reports;
            const world_objects::CarPartMask struck = m_cars->humanHit(*attacked, feet, &reports);
            m_print(std::format("objects: car {:.0f} hit, parts {:#x}\n", *attacked, struck));
            // A car hit is damage done from where the player stands (message 6 from the boxes he is in), and the car
            // reports each damaged part (its message 0x19).
            if (world_objects::ObjectServices* services = m_objects->world.services; services != nullptr) {
                services->damageDone(playerHandle(), *attacked);
                for (const world_objects::CarHitReport& report : reports) {
                    services->carHit(*attacked, playerHandle(), report.part, report.broke);
                }
            }
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
    } else if (const std::optional<combat::StrikeWindow> window =
                   combat::movingStrikeWindow(static_cast<int>(human.animator().animId()))) {
        // A moving attack strikes with its body while its window is open, each object once.
        if (m_strikeAge >= window->on && m_strikeAge < window->off) {
            const int animId = static_cast<int>(human.animator().animId());
            const float reach = animId == combat::anim_id::kRunningAttackDive ? kDiveStrikeReach : kMovingStrikeReach;
            if (const std::optional<double> object = strikeAhead(animId, reach)) {
                m_struck.push_back(*object);
            }
        }
    } else if (const int animId = human.fighter().last().hitAnim; animId != combat::anim_id::kNone) {
        // The attack's reach as the fighter measures it: its far range, else the default reach.
        float reach = human::kDefaultStrikeReach;
        if (const combat::AnimRangeList* ranges = human.ranges(); ranges != nullptr && animId >= 0) {
            if (const float far = ranges->farRange(static_cast<std::size_t>(animId)); far > 0.0F) {
                reach = far;
            }
        }
        static_cast<void>(strikeAhead(animId, reach));
    }
    for (int tick = 0; tick < kObjectTicksPerStep; ++tick) {
        m_objects->tick();
    }
    // A broken barrier removed at its update: its handlers hear message 2 and its record goes.
    for (const double removed : m_objects->doors.takeRemoved()) {
        m_print(std::format("objects: {:.0f} removed\n", removed));
        if (m_pickups != nullptr) {
            m_pickups->objectRemoved(removed);
        }
    }
}

std::optional<double> PlayLevelMode::strikeAhead(int animId, float reach) {
    if (m_objects == nullptr || m_objects->world.collision == nullptr) {
        return std::nullopt;
    }
    const human::Human& human = m_player->human();
    const anim::Vec3 feet = human.position();
    const anim::Vec3 ahead = human::facing(human.heading());
    const raycast::Ray ray{
        .origin = {feet.x, feet.y, feet.z + kStrikeHeight}, .direction = {ahead.x, ahead.y, ahead.z}, .length = reach};
    const std::optional<raycast::RayHit> struck = m_objects->world.collision->rayCast(ray, {}, 0);
    if (!struck) {
        return std::nullopt;
    }
    const std::optional<double> object = m_objects->objectOfTriangle(struck->triangle);
    if (!object || std::ranges::find(m_struck, *object) != m_struck.end()) {
        return std::nullopt;
    }
    const human::GateInput gate = human.gateInput();
    const anim::Vec3 point{feet.x + (ahead.x * struck->t), feet.y + (ahead.y * struck->t), feet.z + kStrikeHeight};
    const bool took = m_objects->humanHit(
        *object, world_objects::ObjectHit{
                     .attacker = playerHandle(),
                     .kind = world_objects::humanHitKind((gate.flags & kRunAttackOrCharge) != 0, gate.airborne),
                     .point = point,
                     .direction = ahead,
                     .attackerAt = feet});
    m_print(std::format("objects: hit {:.0f} with clip {}{}\n", *object, animId, took ? "" : " (no effect)"));
    return object;
}

} // namespace coney::platform
