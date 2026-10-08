// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's part of the level's glass panes and doors (docs/research/objects.md#coneys-implementation): their
// world, their ticks, player 1's hits on them and the lock pick (docs/research/crimes.md#lockpick).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/dealer_goal.h"
#include "ai/goal.h"
#include "ai/swap_prompt.h"
#include "characters/anim_set.h"
#include "characters/character_class.h"
#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/attacks.h"
#include "combat/player_combat.h"
#include "combat/stick_games.h"
#include "combat/throw_velocity.h"
#include "hud/hud.h"
#include "human/fighter.h"
#include "human/fighter_clips.h"
#include "human/human.h"
#include "human/human_sounds.h"
#include "human/humans.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"
#include "platform/play_level_mode.h"
#include "raycast/collision_mesh.h"
#include "scripting/object_bindings.h"
#include "scripting/sound_bindings.h"
#include "warriors/inventory.h"
#include "world_objects/glass.h"
#include "world_objects/object_bodies.h"
#include "world_objects/props.h"

namespace coney::platform {

namespace {

// A segment strike shape meets a pane's body as spheres of its radius along it, at most this far apart (**Coney's
// reading**: the pane test takes spheres).
constexpr float kPaneSampleStep = 0.05F;
// How near a world object must be to be offered as a target: the square's 2 m search (docs/research/combat.md#targets).
constexpr float kObjectTargetReach = 2.0F;
// The strike test looks at world objects within this of the feet (Coney's stand-in for "the bodies near the human":
// past every riot prop's body and a strike shape's reach).
constexpr float kPropStrikeSearch = 4.0F;
// Where a spawn record's object stands: its position and rotation (the record keeps the quaternion as {x, y, z, w}).
world_objects::PropPose recordPose(const world_objects::SpawnRecord& record) {
    return world_objects::PropPose{
        .position = anim::Vec3{record.position[0], record.position[1], record.position[2]},
        .rotation = anim::Quat{record.rotation[0], record.rotation[1], record.rotation[2], record.rotation[3]}};
}
// A dropped object is kept within this many metres × the human's scale, less 0.01 m, of him across the ground
// (docs/research/objects.md#held).
constexpr float kDropReach = 0.35F;
constexpr float kDropReachLess = 0.01F;
// **Coney's stand-in**: an object with no hand pose to fall from (no take event) starts this high above the feet.
constexpr float kDropHeight = 1.0F;
// A throw looks for its target this far away: 10 m with an overhead object (set 4), else 20 m
// (docs/research/objects.md#throws).
constexpr float kThrowSearchOverhead = 10.0F;
constexpr float kThrowSearch = 20.0F;
// **Coney's stand-in** for the target's head bone (bone 6): this high above his feet, times his scale.
constexpr float kThrowTargetHeight = 1.6F;
// A thrown object of anim set 1 or 2 (a knife, a baton) spins at this many rad/s about the world's x axis; anything
// else about z at −k π rad/s, k drawn in [kThrowSpinLeast, kThrowSpinMost] (`MeleeWeapon_Detach`,
// docs/research/objects.md#held).
constexpr float kThrowTumble = -4.0F * std::numbers::pi_v<float>;
constexpr float kThrowSpinLeast = 5.0F;
constexpr float kThrowSpinMost = 7.0F;
// The five knives (`dyn_bowie`, `dyn_hunter`, `dyn_tknife`, `dyn_swhbld`, `dyn_swhbld_super`, by their model hashes)
// take the holder's rotation and tumble at this rate even when dropped (docs/research/objects.md#held).
constexpr std::array<std::uint32_t, 5> kKnifeModels{0x2a263bb3U, 0x8c6fbfe3U, 0x9babb2c7U, 0x876890ddU, 0x39b7cc9aU};
constexpr float kKnifeTumble = -8.0F * std::numbers::pi_v<float>;

// The holder's world rotation: his heading about the world's z axis (facing() turns local +y by it).
anim::Quat headingRotation(float heading) {
    return anim::Quat{.x = 0.0F, .y = 0.0F, .z = std::sin(heading / 2.0F), .w = std::cos(heading / 2.0F)};
}

// Whether `animId` is one of the held-object throws, whose release event lets go of what is held.
bool throwClip(std::uint32_t animId) {
    constexpr std::array<int, 9> kThrows{
        combat::anim_id::kOneHandedThrow,        combat::anim_id::kOneHandedThrowFromWalk,
        combat::anim_id::kOneHandedThrowFromRun, combat::anim_id::kBarrelThrow,
        combat::anim_id::kBarrelThrowFromWalk,   combat::anim_id::kBarrelThrowFromRun,
        combat::anim_id::kGhettoThrow,           combat::anim_id::kGhettoThrowFromWalk,
        combat::anim_id::kGhettoThrowFromRun};
    return std::ranges::find(kThrows, static_cast<int>(animId)) != kThrows.end();
}
// A car stereo's context record (kind 3) reaches this far in the ground plane (`CfgActionDistance`'s default).
constexpr float kStereoReach = 2.0F;
// The player's Warrior class byte `+0x0b`, which makes a theft's stage 3 turns (docs/research/combat.md#stereo-theft).
constexpr std::uint8_t kPlayerTheftByte = 2;
// **Coney's stand-in** for the reach of the pickable door's kind-2 record (not traced): triangle within this many
// metres of the door's position starts a pick.
constexpr float kLockPickReach = 1.5F;
// The record bits a run attack, a charge or a dive holds, which make a human's hit kind 2 (docs/research/objects.md).
constexpr std::uint32_t kRunAttackOrCharge = 0x1400000;
// The record bits with which a strike on the level makes no sound (Strike_Contact).
constexpr std::uint32_t kLevelStrikeQuiet = 0x400800;
// The attacker's anim ids whose strikes on the level make no sound.
constexpr std::array<std::uint32_t, 3> kLevelStrikeQuietAnims{2, 4, 0x1b2};
// The volume of a strike on the level (Strike_Contact's 116, which the engine's 1 caps).
constexpr float kLevelStrikeVolume = 116.0F;
// A player's strike on a car sounds at half volume, which his doubling makes whole.
constexpr float kPlayerCarStrikeVolume = 0.5F;
// What the strike shapes mark struck once one has met the level (no object has this handle).
constexpr double kLevelStruck = -1.0;

// Reports the sound of a human's strike on the level, an object or a car at his feet: his fist, or his body
// (`HUMAN`) while he charges, against `struck` (docs/research/sound-events.md#strike-object).
// @orig 0x0021b290 Strike_Contact (unknown)
void reportStrikeSound(human::Human& human, std::uint32_t struck, float volume, bool player) {
    const bool charging = (human.gateInput().flags & kRunAttackOrCharge) != 0;
    human.reportSound(human::HumanSound{.kind = human::HumanSound::Kind::Impact,
                                        .material1 = charging ? human::material::kHuman : human::material::kFist,
                                        .material2 = struck,
                                        .volume = volume,
                                        .victimDown = false,
                                        .ownerIsPlayer = player,
                                        .at = human.position()});
}
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
// The prompts' GSTRING.HUD ids (docs/research/crimes.md#context-records): a held human to mug, a pickable door, a car
// stereo.
constexpr std::uint32_t kMugPrompt = 1;
constexpr std::uint32_t kInterrogatePrompt = 0;
constexpr std::uint32_t kLockPrompt = 15;
constexpr std::uint32_t kStereoPrompt = 16;
// The interface cues of the stereo theft: each stage completed, and also the fourth
// (docs/research/hud.md#stereo-layout).
constexpr int kStageCue = 0x22;
constexpr int kLastStageCue = 0x23;

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
            const double attacker = handleOf(human);
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
    // A human's switched-on strike shapes strike the panes and doors they meet
    // (docs/research/combat.md#moving-strikes).
    m_player->humans().setStrikeContact(
        [this](human::Human& human, std::span<const human::PosedShape> shapes) { strikeObjects(human, shapes); });
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
        // A Warrior's swap, asked through the pick-up search (**Coney's stand-in**: before the objects, not by
        // distance among them).
        if (!m_pickups->actionObject(feet).has_value() && trySwap(human, blocked)) {
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
            // At once, with no clip: it falls from the hand.
            const double held = std::exchange(script.heldObject, world_objects::kNoObject);
            script.heldObjectName.clear();
            dropHeld(human, held);
            const world_objects::SpawnRecord* record = m_records != nullptr ? m_records->find(held) : nullptr;
            const std::array<float, 3> from = record != nullptr ? record->position : std::array<float, 3>{};
            m_print(std::format("pickup: dropped object {:.0f} from ({:.2f}, {:.2f}, {:.3f}), feet at z {:.3f}\n", held,
                                from[0], from[1], from[2], feet.z));
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

const world_objects::Door* PlayLevelMode::lockInReach(anim::Vec3 feet) const {
    if (m_objects == nullptr) {
        return nullptr;
    }
    const world_objects::Door* nearest = nullptr;
    float nearestDistance = kLockPickReach;
    for (const world_objects::Door& door : m_objects->doors.doors()) {
        const float distance = std::hypot(door.position.x - feet.x, door.position.y - feet.y);
        if (door.pickable && !door.ended && distance <= nearestDistance) {
            nearest = &door;
            nearestDistance = distance;
        }
    }
    return nearest;
}

PromptOffer PlayLevelMode::promptOffer() const {
    PromptOffer offer;
    const human::Human& human = m_player->human();
    const combat::PlayerCombat& fight = human.fighter().combat();
    // A mugging, a theft or a lock pick holds him: no prompt (HUD_Update's "free to act" test).
    if (m_lockPick || fight.mode() == combat::CombatMode::Mugging || fight.mode() == combat::CombatMode::Theft ||
        m_mugEnding.has_value()) {
        offer.blocked = true;
        return offer;
    }
    // Holding a human who can be mugged (Mug_CanMugVictim): one set up for interrogation, whatever he carries
    // ("interrogate", 0), or one who may be mugged and has money or a pocket item (1). **Coney's reading**: no carried
    // object (`+0x257`) is modelled.
    if (fight.mode() == combat::CombatMode::Grabbing) {
        if (const auto* victim = dynamic_cast<const human::Human*>(human.fighter().held());
            victim != nullptr && !victim->health().depleted()) {
            const human::ScriptState& script = victim->script();
            if (!script.interrogationCallback.empty()) {
                offer.held = kInterrogatePrompt;
            } else if (script.muggable && (script.money > 0 || script.pocketCount > 0)) {
                offer.held = kMugPrompt;
            }
        }
    }
    const anim::Vec3 feet = human.position();
    if (lockInReach(feet) != nullptr) {
        offer.lock = kLockPrompt;
    }
    if (stereoInReach(feet) != nullptr) {
        offer.stereo = kStereoPrompt;
    }
    // A dealer whose offer's record is up (registered by his last update) and within its reach.
    if (m_ai != nullptr) {
        const ai::Brains& brains = m_ai->brains();
        for (std::size_t i = 0; i < brains.size() && !offer.dealer; ++i) {
            const ai::Brain& brain = brains.at(i);
            const ai::Goal* goal = brain.topGoal();
            if (goal == nullptr || goal->type() != ai::GoalType::Dealer) {
                continue;
            }
            const auto& dealer = static_cast<const ai::DealerGoal&>(*goal);
            const anim::Vec3 at = brain.human().position();
            const std::optional<ai::DealTerms> terms = ai::dealTerms(dealer.type());
            if (terms && dealer.prompting() && std::hypot(at.x - feet.x, at.y - feet.y) <= ai::kDealReach &&
                std::fabs(at.z - (feet.z + 1.0F)) <= LevelPickups::kPromptHeight) {
                offer.dealer = static_cast<std::uint32_t>(terms->prompt);
            }
        }
    }
    return offer;
}

bool PlayLevelMode::trySwap(human::Human& human, const world_objects::SightBlocked& blocked) {
    if (m_ai == nullptr || m_pickups == nullptr) {
        return false;
    }
    const anim::Vec3 feet = human.position();
    const anim::Vec3 ahead = human::facing(human.heading());
    // The Warriors ahead of him within 1.5 m, nearest first, each asked in turn (event 0); a refusal asks the next.
    ai::Brains& brains = m_ai->brains();
    std::vector<std::pair<float, ai::Brain*>> asked;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        ai::Brain& brain = brains.at(i);
        const anim::Vec3 at = brain.human().position();
        const float dx = at.x - feet.x;
        const float dy = at.y - feet.y;
        const float distance = std::sqrt((dx * dx) + (dy * dy) + ((at.z - feet.z) * (at.z - feet.z)));
        if (brain.type() == ai::BrainType::Warrior && &brain.human() != &human && distance <= ai::kSwapReach &&
            (dx * ahead.x) + (dy * ahead.y) >= 0.0F) {
            asked.emplace_back(distance, &brain);
        }
    }
    std::ranges::sort(asked, {}, &std::pair<float, ai::Brain*>::first);
    for (const auto& [distance, brain] : asked) {
        human::Human& receiver = brain->human();
        // WarriorBrain_OnPrompt's refusals: not talkable; either busy (**Coney's stand-in** for the blocked actions
        // and the held flags: out of the free combat mode); the receiver grabbed or the way to the presser blocked.
        const anim::Vec3 from = receiver.position();
        constexpr float kChest = 1.0F;
        if (!receiver.script().talkable || human.fighter().combat().mode() != combat::CombatMode::Free ||
            receiver.fighter().combat().mode() != combat::CombatMode::Free || receiver.fighter().grabbed() ||
            blocked(anim::Vec3{from.x, from.y, from.z + kChest}, anim::Vec3{feet.x, feet.y, feet.z + kChest})) {
            continue;
        }
        // He stops what he does and turns to the presser over 0.2 s; the presser does not turn.
        brain->clearActions();
        constexpr float kTurnSeconds = 0.2F;
        receiver.turnToFace(feet, kTurnSeconds);
        // The swap, at once: what cannot go into a hand stays at its giver's feet.
        const double presserHad = human.script().heldObject;
        const ai::SwapOutcome outcome =
            ai::swapHeldObjects(receiver.script(), human.script(), [this](std::string_view typeName) {
                const world_objects::ObjectType* type =
                    m_objectTypes != nullptr ? m_objectTypes->find(typeName) : nullptr;
                return type == nullptr || ai::swapPlaceable(type->pickupAnim);
            });
        for (const double object : outcome.dropped) {
            m_pickups->drop(object, object == presserHad ? feet : from);
        }
        // Player 1's hand changed by the swap, not by a drop: stepPickups() must not drop the old object.
        m_heldObject = human.script().heldObject;
        receiver.fighter().setAnimSet(m_pickups->animSetOf(receiver.script().heldObjectName));
        // His line when he now holds something and is not hidden in shadow.
        if (human.script().heldObject != world_objects::kNoObject && !human.hidden() && m_sound != nullptr) {
            static_cast<void>(m_sound->sayCommand(
                script::CommandCall{.human = playerHandle(), .command = ai::kGiveMeCommand, .interrupt = false}, {}));
        }
        m_print(std::format("swap: with human {:.0f}, player 1 now holds {:.0f}, he holds {:.0f}\n", brain->handle(),
                            human.script().heldObject, receiver.script().heldObject));
        return true;
    }
    return false;
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

const world_objects::ObjectType* PlayLevelMode::worldObjectType(double handle) const {
    if (m_records == nullptr || m_objectTypes == nullptr) {
        return nullptr;
    }
    const world_objects::SpawnRecord* record = m_records->find(handle);
    if (record == nullptr || record->removed) {
        return nullptr;
    }
    const world_objects::ObjectType* type = m_objectTypes->find(record->typeName);
    return type != nullptr && world_objects::isStrikeTarget(world_objects::bodyFlagsOf(type->bodyWord)) ? type
                                                                                                        : nullptr;
}

void PlayLevelMode::stepObjectBodies() {
    m_objectBodies.clear();
    if (m_records == nullptr || m_objectTypes == nullptr) {
        m_runContacts.clear();
        return;
    }
    // A running human's contact with a RUNTARGET body strikes it, once (a plain hit at his walking sphere).
    for (const auto& [attacker, feet, object] : std::exchange(m_runContacts, {})) {
        const world_objects::SpawnRecord* record = m_records->find(object);
        const world_objects::ObjectType* type = worldObjectType(object);
        if (record != nullptr && type != nullptr && m_objects != nullptr && !m_objects->props.broken(object)) {
            const world_objects::PropPose pose = recordPose(*record);
            strikeProp(attacker, object, *type, world_objects::HitKind::Plain, feet,
                       anim::normalise(anim::subtract(pose.position, feet)), feet, pose);
        }
    }
    // Each object in the world and not in a hand or on a head whose type has a body and a layer; a broken prop has
    // lost its body.
    for (const world_objects::SpawnRecord& record : m_records->all()) {
        if (record.removed || !m_objectTasks.inWorld(record.handle) || m_wornHats.contains(record.handle) ||
            (m_pickups != nullptr && m_pickups->inHand(record.handle)) ||
            (m_objects != nullptr && m_objects->props.broken(record.handle))) {
            continue;
        }
        const world_objects::ObjectType* type = m_objectTypes->find(record.typeName);
        if (type == nullptr || type->bodyWord == 0) {
            continue;
        }
        if (const std::optional<world_objects::ObjectBody> body = world_objects::bodyOf(record, *type)) {
            m_objectBodies.add(*body);
        }
    }
    // A walking human slides along the `BLOCKHUMANS` ones; one above jog notes the `RUNTARGET` ones he meets.
    m_player->humans().setObjectPush([this](const human::Human& walker, anim::Vec3 centre, float radius,
                                            anim::Vec3 move) {
        if (walker.gait() > human::Gait::Jog) {
            for (const double object : m_objectBodies.touching(centre, radius, world_objects::kPhyRunTarget)) {
                const double attacker = handleOf(walker);
                if (std::ranges::none_of(m_runContacts,
                                         [object](const auto& contact) { return std::get<2>(contact) == object; })) {
                    m_runContacts.emplace_back(attacker, walker.position(), object);
                }
            }
        }
        return m_objectBodies.pushOut(centre, radius, world_objects::kPhyBlockHumans, move);
    });
}

void PlayLevelMode::giveObjectTargets() {
    if (m_objects == nullptr) {
        return;
    }
    std::vector<human::ObjectTarget> objects;
    // The world objects in the world whose body makes them a strike target (a street prop, a trash can), not broken,
    // within 2 m in height and near enough for the square's reach (docs/research/combat.md#targets).
    if (m_records != nullptr && m_objectTypes != nullptr) {
        const anim::Vec3 feet = m_player->human().position();
        for (const world_objects::ObjectDraw& draw : m_objectTasks.draws()) {
            if (draw.column || worldObjectType(draw.handle) == nullptr || m_objects->props.broken(draw.handle) ||
                std::fabs(draw.position.z - feet.z) > human::kPickHeight ||
                std::hypot(draw.position.x - feet.x, draw.position.y - feet.y) > kObjectTargetReach) {
                continue;
            }
            objects.push_back(human::ObjectTarget{.handle = draw.handle, .point = draw.position});
        }
    }
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
    stepStereoPanel(human);
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
    // A throw's release event lets go of the held object, thrown (Human_ReleaseThrow on event 10).
    if (human.takeThrowRelease() && throwClip(human.animator().animId()) &&
        script.heldObject != world_objects::kNoObject) {
        const double held = std::exchange(script.heldObject, world_objects::kNoObject);
        script.heldObjectName.clear();
        dropHeld(human, held, true);
        const world_objects::LooseObject* flying = m_looseObjects.find(held);
        const anim::Vec3 v = flying != nullptr ? flying->velocity : anim::Vec3{};
        m_print(std::format("pickup: threw object {:.0f} at ({:.2f}, {:.2f}, {:.2f}) m/s\n", held, v.x, v.y, v.z));
    }
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
    // An object let go some other way (HuDropWeapon, ObjDestroy) falls from the hand too; the anim set follows the
    // hand. The triangle drop has already let it go.
    if (m_heldObject != world_objects::kNoObject && m_heldObject != script.heldObject &&
        m_pickups->inHand(m_heldObject)) {
        dropHeld(human, m_heldObject);
    }
    m_heldObject = script.heldObject;
    human.fighter().setAnimSet(m_pickups->animSetOf(script.heldObjectName));
}

void PlayLevelMode::stepStereoPanel(const human::Human& human) {
    hud::Hud& hud = m_hud->hud();
    hud::StereoHud& panel = hud.stereo(0);
    const std::optional<combat::StereoTheft>& theft = human.fighter().combat().theft();
    if (!m_theftCar || !theft) {
        // A theft cut short (a hit, a knock-down, death, a scene: Human::stereoTheftPlaying()) has no outcome: the car
        // is forgotten and the panel goes. An ended one keeps the car until its outcome is taken below.
        if (m_theftCar && human.fighter().last().game == combat::GameResult::Running) {
            m_print(std::format("theft: cut short at car {:.0f}\n", *m_theftCar));
            m_theftCar.reset();
        }
        panel.end();
        return;
    }
    // Shown from the triangle press, with the intro; each completed stage plays cue 0x22.
    if (!panel.shown()) {
        panel.start(theft->stageTarget());
        m_theftStage = 0;
    }
    const combat::GameResult result = human.fighter().last().game;
    if (theft->stage() > m_theftStage || result == combat::GameResult::Succeeded) {
        hud.services().sound.playCue(kStageCue);
    }
    m_theftStage = theft->stage();
    panel.setProgress(theft->turned(), theft->stage());
    // The success (with cue 0x23 for the fourth stage) or failure clip starts: the panel goes at once.
    if (result != combat::GameResult::Running) {
        if (result == combat::GameResult::Succeeded) {
            hud.services().sound.playCue(kLastStageCue);
        }
        panel.end();
    }
}

void PlayLevelMode::stepMugMeter(const human::Human& human, const Pad& pad) {
    hud::MugMeter& meter = m_hud->hud().mug(0);
    const combat::PlayerCombat& fight = human.fighter().combat();
    const std::optional<combat::MuggingGame>& game = fight.mugging();
    if (fight.mode() != combat::CombatMode::Mugging || !game) {
        meter.setActive(false);
        return;
    }
    meter.setActive(true);
    meter.setMode(hud::MugMeterMode::Mugging);
    meter.setStick(pad.leftX(), pad.leftY());
    meter.setOnTarget(game->onTarget());
    // Bar 2 is the time on target against the time required. Bar 1 is the time used: Coney's player mugging fails on
    // its off-target allowance (record +0x0c), so the off-target time against it stands in for the original's time
    // since the start against the time allowed (+0x130, +0x134).
    const combat::MuggingParams& params = game->params();
    const auto share = [](std::uint64_t part, int whole) {
        return whole > 0 ? static_cast<float>(part) / static_cast<float>(whole) : 0.0F;
    };
    meter.setFills(share(game->offTargetMs(), params.offTargetMs), share(game->progressMs(), params.requiredMs));
}

void PlayLevelMode::stepLockPickDial() {
    hud::LockPickHud& dial = m_hud->hud().lockPick(0);
    if (!m_lockPick) {
        if (dial.shown()) {
            dial.show(false);
        }
        return;
    }
    if (!dial.shown()) {
        dial.show(true, m_lockPick->dial().difficulty());
    }
    const world_objects::LockPickDial& pins = m_lockPick->dial();
    dial.setPins(pins.pins(), std::max(pins.good(), 0));
}

void PlayLevelMode::dropHeld(human::Human& human, double held, bool thrown) {
    world_objects::SpawnRecord* record = m_records != nullptr ? m_records->find(held) : nullptr;
    if (record == nullptr || m_objectTypes == nullptr) {
        m_pickups->drop(held, human.position());
        return;
    }
    // It starts where the hand holds it, thrown or dropped (a throw from the aiming state, which starts at the aim's
    // release point, is not built), or above the feet when the hand pose is unknown.
    const anim::Vec3 feet = human.position();
    world_objects::WorldPose start =
        heldPose(held, m_player->current())
            .value_or(world_objects::WorldPose{
                .position = {feet.x, feet.y, feet.z + kDropHeight},
                .rotation = {record->rotation[0], record->rotation[1], record->rotation[2], record->rotation[3]}});
    // A drop is pulled back across the ground to within reach of the human.
    const float reach = (kDropReach * human.scale()) - kDropReachLess;
    const float dx = start.position.x - feet.x;
    const float dy = start.position.y - feet.y;
    if (const float across = std::hypot(dx, dy); !thrown && across > reach && across > 0.0F) {
        start.position.x = feet.x + (dx * reach / across);
        start.position.y = feet.y + (dy * reach / across);
    }
    // The detach (MeleeWeapon_Detach): one of the five knives takes the holder's rotation and tumbles at 8π rad/s,
    // thrown or dropped. Otherwise a drop gives no velocity and no spin; a throw's velocity
    // (Human_ComputeThrowVelocity, in the holder's frame) is turned by his rotation, and an object of anim set 1 or 2
    // takes his rotation and tumbles at 4π, anything else keeps its own and spins about z. Both spins are in the
    // world's axes.
    const world_objects::ObjectType* type = m_objectTypes->find(record->typeName);
    const bool knife = type != nullptr && std::ranges::find(kKnifeModels, type->modelHash) != kKnifeModels.end();
    anim::Vec3 velocity{};
    anim::Vec3 spin{};
    if (thrown) {
        const anim::Quat holder = headingRotation(human.heading());
        const anim::Mat34 turn = anim::matrixFromQuat(holder);
        const int set = m_pickups->animSetOf(record->typeName);
        combat::ThrowAim aim{.weightFactor =
                                 combat::throwWeightFactor(type != nullptr ? type->objectKind : 0,
                                                           type != nullptr ? type->weight : 0, human.scale()),
                             .fastDefault = human.gait() > human::Gait::Walk && (set == 4 || set == 6)};
        // At the player's target within the set's search radius, from the hand. **Coney's stand-ins**: the fighter's
        // target for `Player_PickThrowTarget`'s pick, the held object's place for the posed hand (bone 25), and a
        // fixed height on the target's scale for his head bone. A player's throw has no spread (flag 0x40000).
        if (const human::Combatant* target = human.fighter().target(); target != nullptr) {
            const anim::Vec3 at = target->position();
            const anim::Vec3 point{at.x, at.y, at.z + (kThrowTargetHeight * target->bodyScale())};
            const anim::Vec3 world = anim::subtract(point, start.position);
            const float radius = set == 4 ? kThrowSearchOverhead : kThrowSearch;
            if (std::hypot(at.x - feet.x, at.y - feet.y) <= radius) {
                aim.toTarget = anim::transformDirection(anim::inverseRigid(turn), world);
            }
        }
        velocity = anim::transformDirection(turn, combat::throwVelocity(aim));
        if (set == 1 || set == 2) {
            start.rotation = holder;
            spin = anim::Vec3{kThrowTumble, 0.0F, 0.0F};
        } else if (!knife) {
            const float k = kThrowSpinLeast + ((kThrowSpinMost - kThrowSpinLeast) * m_throwRandom.unit());
            spin = anim::Vec3{0.0F, 0.0F, -k * std::numbers::pi_v<float>};
        }
    }
    if (knife) {
        start.rotation = headingRotation(human.heading());
        spin = anim::Vec3{kKnifeTumble, 0.0F, 0.0F};
    }
    m_pickups->drop(held, start.position);
    record->rotation = {start.rotation.x, start.rotation.y, start.rotation.z, start.rotation.w};
    m_looseObjects.start(held, start.position, start.rotation,
                         type != nullptr ? world_objects::LooseObjects::Kind::of(*type)
                                         : world_objects::LooseObjects::Kind{},
                         velocity, spin);
}

void PlayLevelMode::stepLooseObjects() {
    if (m_records == nullptr) {
        m_looseObjects.clear();
        return;
    }
    // An object picked up again or gone from the level leaves the flight (WorldObject_Remove cancels its settle).
    std::vector<double> gone;
    for (const auto& [handle, object] : m_looseObjects.all()) {
        const world_objects::SpawnRecord* record = m_records->find(handle);
        if (record == nullptr || record->removed || (m_pickups != nullptr && m_pickups->inHand(handle))) {
            gone.push_back(handle);
        }
    }
    for (const double handle : gone) {
        m_looseObjects.remove(handle);
    }
    // The move meets the level's collision mesh. **Coney stand-in**: humans, cars and other objects (the bodies with
    // `BLOCKOBJECTS`) are not met yet.
    const raycast::CollisionMesh* mesh = m_objects != nullptr && m_objects->world.collision != nullptr
                                             ? m_objects->world.collision
                                             : &m_scenery->collision();
    const world_objects::RayTest test = [mesh](anim::Vec3 origin, anim::Vec3 direction,
                                               float length) -> std::optional<world_objects::RayContact> {
        const raycast::Ray ray{.origin = {origin.x, origin.y, origin.z},
                               .direction = {direction.x, direction.y, direction.z},
                               .length = length};
        const std::optional<raycast::RayHit> hit = mesh->rayCast(ray, {}, 0);
        if (!hit) {
            return std::nullopt;
        }
        return world_objects::RayContact{
            .distance = hit->t, .normal = {hit->normal.x, hit->normal.y, hit->normal.z}, .body = false};
    };
    // Each object's pose goes back into its record, which the world objects draw.
    const auto place = [this](double handle, const world_objects::LooseObject& object) {
        if (world_objects::SpawnRecord* record = m_records->find(handle)) {
            record->position = {object.position.x, object.position.y, object.position.z};
            record->rotation = {object.rotation.x, object.rotation.y, object.rotation.z, object.rotation.w};
        }
    };
    m_looseObjects.step(test, [this, &place](double handle, const world_objects::LooseObject& object) {
        place(handle, object);
        m_print(std::format("objects: object {:.0f} came to rest at ({:.2f}, {:.2f}, {:.3f})\n", handle,
                            object.position.x, object.position.y, object.position.z));
    });
    for (const auto& [handle, object] : m_looseObjects.all()) {
        place(handle, object);
    }
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
    // TODO(mission1-finish, the mugging's owner; docs/research/crimes.md#interrogation): a victim set up for
    // interrogation (ScriptState::interrogationCallback, HuSetInterrogation) says its set lines instead of the
    // mugging's, pays nothing, and on success says the fourth line, has its callback called with (victim, mugger,
    // true) and the interrogation cleared (`0x00226168`). Only the "interrogate" prompt is built (promptOffer()).
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
        const world_objects::Door* nearest = lockInReach(feet);
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
    // Player 1's object attack strikes its object when its clip's strike shapes reach it (Strike_Contact), not at the
    // attack's start: the shapes strike the panes and doors they meet in the humans' step (strikeObjects()).
    const human::Human& human = m_player->human();
    if (const std::optional<double> aimed = human.fighter().objectHit()) {
        m_objectAttack = ObjectAttack{.object = *aimed, .clip = human.animator().animId()};
    }
    if (const std::optional<double> attacked = objectAttackLands(human)) {
        const anim::Vec3 feet = human.position();
        const anim::Vec3 ahead = human::facing(human.heading());
        // A car takes the hit itself (Strike_Contact calls its hit handler; no message 1), by where the player stands.
        if (m_cars != nullptr && m_cars->find(*attacked) != nullptr) {
            std::vector<world_objects::CarHitReport> reports;
            const world_objects::CarPartMask struck = m_cars->humanHit(*attacked, feet, &reports);
            m_print(std::format("objects: car {:.0f} hit, parts {:#x}\n", *attacked, struck));
            // A strike that reached a part sounds on the hood.
            if (struck != 0) {
                reportStrikeSound(m_player->human(), human::material::kCarHood, kPlayerCarStrikeVolume, true);
            }
            // A car hit is damage done from where the player stands (message 6 from the boxes he is in), and the car
            // reports each damaged part (its message 0x19).
            if (world_objects::ObjectServices* services = m_objects->world.services; services != nullptr) {
                services->damageDone(playerHandle(), *attacked);
                for (const world_objects::CarHitReport& report : reports) {
                    services->carHit(*attacked, playerHandle(), report.part, report.broke);
                }
            }
        } else if (const world_objects::ObjectType* type = worldObjectType(*attacked); type != nullptr) {
            // A world object (a street prop): its counters, the boxes' message 6 while it was intact, its own hit;
            // nothing more when the attack's shapes struck it already (the strike reaches it once).
            human::StrikeShapes& strikes = m_player->human().strikeShapes();
            if (!strikes.struck(*attacked) && !m_objects->props.broken(*attacked)) {
                strikes.markStruck(*attacked);
                const world_objects::SpawnRecord* record = m_records->find(*attacked);
                strikeProp(playerHandle(), *attacked, *type, world_objects::humanHitKind(false, false),
                           recordPose(*record).position, ahead, feet, recordPose(*record));
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
    // A broken prop goes for good at its update: its handlers hear message 2 and its record is never spawned again.
    for (const double removed : m_objects->props.takeRemoved()) {
        m_print(std::format("objects: prop {:.0f} removed\n", removed));
        if (m_pickups != nullptr) {
            m_pickups->objectRemoved(removed);
        } else if (m_records != nullptr) {
            static_cast<void>(m_records->destroy(removed));
        }
    }
    // The car parts that came off this step: the hook for their shatter, sound and fall (carBreaks()).
    m_carBreaks.clear();
    if (m_cars != nullptr) {
        m_carBreaks = m_cars->takeBreaks();
        for (const world_objects::CarPartBreak& broke : m_carBreaks) {
            m_print(std::format("objects: car {:.0f} part {} off{}\n", broke.car, broke.part,
                                broke.window ? " (a window)" : ""));
        }
    }
}

std::optional<double> PlayLevelMode::objectAttackLands(const human::Human& human) {
    if (!m_objectAttack) {
        return std::nullopt;
    }
    const ObjectAttack attack = *m_objectAttack;
    const human::StrikeShapes& strikes = human.strikeShapes();
    // The clip ended. **Coney's stand-in**: a clip whose strike shapes never came on (no strike events) lands its hit
    // as it ends; one whose window passed without reaching a car or object landed it then.
    if (human.animator().animId() != attack.clip) {
        m_objectAttack.reset();
        return attack.shapesOn ? std::nullopt : std::optional<double>(attack.object);
    }
    const bool shaped =
        m_objects->glass.find(attack.object) != nullptr || m_objects->doors.find(attack.object) != nullptr;
    if (strikes.anyOn()) {
        m_objectAttack->shapesOn = true;
        // A car or other object, which Coney's shapes do not strike: at the first update of the strike window.
        if (!shaped) {
            m_objectAttack.reset();
            return attack.object;
        }
        return std::nullopt;
    }
    // **Coney's stand-in**: a pane or door the shapes passed without touching takes the hit as the window closes, so
    // an object attack aimed at it never misses.
    if (attack.shapesOn) {
        m_objectAttack.reset();
        return attack.object;
    }
    return std::nullopt;
}

void PlayLevelMode::strikeObjects(human::Human& human, std::span<const human::PosedShape> shapes) {
    if (m_objects == nullptr || m_objects->world.collision == nullptr) {
        return;
    }
    const raycast::CollisionMesh& mesh = *m_objects->world.collision;
    const human::GateInput gate = human.gateInput();
    const anim::Vec3 feet = human.position();
    // A body not struck since the shapes came on takes message 1, with the hit kind of the attack record.
    human::StrikeShapes& strikes = human.strikeShapes();
    const bool player = &human == &m_player->human();
    const auto strike = [&](double object, anim::Vec3 point, std::uint8_t material) {
        strikes.markStruck(object);
        // Player 1's object attack aimed here has landed (objectAttackLands() stops waiting).
        if (player && m_objectAttack && m_objectAttack->object == object) {
            m_objectAttack.reset();
        }
        // The object sounds as its type's material before it takes the hit.
        reportStrikeSound(human, material, 1.0F, player);
        const bool took = m_objects->humanHit(
            object, world_objects::ObjectHit{
                        .attacker = handleOf(human),
                        .kind = world_objects::humanHitKind((gate.flags & kRunAttackOrCharge) != 0, gate.airborne),
                        .point = point,
                        .direction = human::facing(human.heading()),
                        .attackerAt = feet});
        m_print(std::format("objects: {:.0f} struck by clip {}{}\n", object, human.animator().animId(),
                            took ? "" : " (no effect)"));
    };
    // The doors and barriers: their two triangles, while enabled.
    const auto corner = [&mesh](std::uint32_t triangle, std::size_t i) {
        const raycast::Vec3 v = mesh.vertices()[mesh.triangles()[triangle].vertices[i]];
        return anim::Vec3{v.x, v.y, v.z};
    };
    for (const world_objects::Door& door : m_objects->doors.doors()) {
        if (strikes.struck(door.handle)) {
            continue;
        }
        for (const std::uint32_t triangle : door.triangles) {
            if (triangle >= mesh.triangles().size() ||
                (mesh.triangles()[triangle].flags & raycast::kTriangleEnabled) == 0) {
                continue;
            }
            const auto met = std::ranges::find_if(shapes, [&](const human::PosedShape& shape) {
                return human::shapeTouchesTriangle(shape, corner(triangle, 0), corner(triangle, 1),
                                                   corner(triangle, 2));
            });
            if (met != shapes.end()) {
                strike(door.handle, met->a, door.material);
                break;
            }
        }
    }
    // The world objects that are strike targets, not broken and within reach of the human.
    std::vector<std::pair<const world_objects::ObjectDraw*, const world_objects::ObjectType*>> props;
    for (const world_objects::ObjectDraw& draw : m_objectTasks.draws()) {
        const world_objects::ObjectType* type = draw.column ? nullptr : worldObjectType(draw.handle);
        if (type != nullptr && !m_objects->props.broken(draw.handle) && !strikes.struck(draw.handle) &&
            anim::distance(draw.position, feet) <= kPropStrikeSearch) {
            props.emplace_back(&draw, type);
        }
    }
    // The panes and props: each shape as spheres along it against their bodies.
    for (const human::PosedShape& shape : shapes) {
        const float length = anim::distance(shape.a, shape.b);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / kPaneSampleStep)));
        for (int i = 0; i <= steps; ++i) {
            const anim::Vec3 at = anim::lerp(shape.a, shape.b, static_cast<float>(i) / static_cast<float>(steps));
            for (const double pane : m_objects->glass.bodiesTouching(at, shape.radius)) {
                if (!strikes.struck(pane)) {
                    strike(pane, at, world_objects::material::kGlass);
                }
            }
            for (const auto& [draw, type] : props) {
                if (!strikes.struck(draw->handle) &&
                    world_objects::bodyTouches(*type, draw->position, draw->rotation, at, shape.radius)) {
                    strikes.markStruck(draw->handle);
                    strikeProp(handleOf(human), draw->handle, *type,
                               world_objects::humanHitKind((gate.flags & kRunAttackOrCharge) != 0, gate.airborne), at,
                               human::facing(human.heading()), feet,
                               world_objects::PropPose{.position = draw->position, .rotation = draw->rotation});
                }
            }
        }
    }
    // Then the level itself.
    strikeLevel(human, shapes, player);
}

void PlayLevelMode::strikeLevel(human::Human& human, std::span<const human::PosedShape> shapes, bool player) {
    if (m_objects == nullptr || m_objects->world.collision == nullptr) {
        return;
    }
    human::StrikeShapes& strikes = human.strikeShapes();
    const std::uint32_t anim = human.animator().animId();
    if (strikes.struck(kLevelStruck) || (human.gateInput().flags & kLevelStrikeQuiet) != 0 ||
        std::ranges::find(kLevelStrikeQuietAnims, anim) != kLevelStrikeQuietAnims.end()) {
        return;
    }
    // **Coney's stand-in** for the shapes' contact with the level mesh: each shape's segment as a ray against the
    // level's triangles (an object's triangles are the objects' own strike above); the turn limits and state
    // 0x4000000 that also keep it quiet are not modelled.
    for (const human::PosedShape& shape : shapes) {
        const anim::Vec3 along = anim::subtract(shape.b, shape.a);
        const float length = anim::length(along);
        if (length < 1e-4F) {
            continue;
        }
        const raycast::Ray ray{.origin = {shape.a.x, shape.a.y, shape.a.z},
                               .direction = {along.x / length, along.y / length, along.z / length},
                               .length = length};
        const std::optional<raycast::RayHit> hit = m_objects->world.collision->rayCast(ray, {}, 0);
        if (hit && !m_objects->objectOfTriangle(hit->triangle)) {
            strikes.markStruck(kLevelStruck);
            reportStrikeSound(human, hit->material, kLevelStrikeVolume, player);
            return;
        }
    }
}

void PlayLevelMode::strikeProp(double attacker, double handle, const world_objects::ObjectType& type,
                               world_objects::HitKind kind, anim::Vec3 point, anim::Vec3 direction,
                               anim::Vec3 attackerAt, const world_objects::PropPose& pose) {
    const world_objects::PropStrike strike = m_objects->props.strike(
        handle, type,
        world_objects::ObjectHit{
            .attacker = attacker, .kind = kind, .point = point, .direction = direction, .attackerAt = attackerAt},
        pose, m_objects->world);
    m_print(std::format("objects: {} {:.0f} hit{}{}\n", type.name, handle,
                        strike.intactBefore ? "" : " (already broken)", strike.broke ? ", broke" : ""));
}

double PlayLevelMode::handleOf(const human::Human& human) const {
    if (&human == &m_player->human()) {
        return playerHandle();
    }
    const ai::Brain* brain = m_ai != nullptr ? m_ai->brainOf(human) : nullptr;
    return brain != nullptr ? brain->handle() : world_objects::kNoObject;
}

} // namespace coney::platform
