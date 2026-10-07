// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's hats: each human's class hat put on when he is made, a script's hat put on, the hat drawn on his
// head, knocked off when he goes down, and falling until it lies on the ground (docs/research/characters.md#hats).
#include <cmath>
#include <format>
#include <optional>
#include <utility>

#include "ai/brain.h"
#include "animation/skeleton.h"
#include "characters/character_class.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "platform/play_level_mode.h"
#include "raycast/collision_mesh.h"
#include "world_objects/hats.h"

namespace coney::platform {

namespace {

// The side a knocked-off hat leaves by (`+0x668` & 3). **Coney's stand-in**: the side of the blow is not modelled, so
// every hat is thrown off behind the wearer (2).
constexpr int kKnockSide = 2;

// The character classes whose knockdowns leave the hat on (with brain type 1, the cops).
constexpr int kKeepsHatClassA = 0x80;
constexpr int kKeepsHatClassB = 13;

// How far above a falling hat the ground ray starts, and how far down it reaches.
constexpr float kGroundRayAbove = 0.5F;
constexpr float kGroundRayLength = 50.0F;

} // namespace

void PlayLevelMode::stepHats() {
    if (m_records == nullptr || m_objectTypes == nullptr) {
        return;
    }
    if (!m_hatFitsRead && m_cast.recorded != nullptr) {
        m_hatFits = world_objects::HatFits::fromRecorded(*m_cast.recorded);
        m_hatFitsRead = true;
    }
    // Every human's hat: put on, replaced, knocked off; a deleted human's goes with him.
    const human::PlayerSnapshot& player = m_player->current();
    stepWearer(m_player->human(), m_type, playerCharacter(), player.pose, player.feet, player.heading);
    const std::vector<ai::AiHuman>& fighters = m_ai->humans();
    for (std::size_t i = 0; i < fighters.size(); ++i) {
        human::Human& human = *fighters[i].human;
        if (fighters[i].removed) {
            removeWornHat(human);
            continue;
        }
        const HumanCreation* made = m_cast.humans != nullptr ? m_cast.humans->find(castHandleOf(fighters[i])) : nullptr;
        const human::PlayerCharacter& character = i < m_fighterMeshes.size() && m_fighterMeshes[i].character != nullptr
                                                      ? *m_fighterMeshes[i].character
                                                      : *m_character;
        const human::TargetSnapshot& now = fighters[i].current;
        stepWearer(human, made != nullptr ? std::optional<int>(made->type) : std::nullopt, character, now.pose,
                   now.feet, now.heading);
    }
    // The hats in flight fall; each one's record follows it, and stays where it lands.
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    const auto ground = [&mesh](anim::Vec3 at) -> std::optional<float> {
        const raycast::Ray ray{.origin = raycast::Vec3{at.x, at.y, at.z + kGroundRayAbove},
                               .direction = raycast::Vec3{0.0F, 0.0F, -1.0F},
                               .length = kGroundRayLength};
        const std::optional<raycast::RayHit> hit = mesh.rayCast(ray, {}, 0);
        return hit ? std::optional<float>(at.z + kGroundRayAbove - hit->t) : std::nullopt;
    };
    for (const auto& [hat, pose] : m_fallingHats.step(1.0F / 30.0F, ground)) {
        if (world_objects::SpawnRecord* record = m_records->find(hat); record != nullptr) {
            record->position = {pose.position.x, pose.position.y, pose.position.z};
            record->rotation = {pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w};
        } else {
            m_fallingHats.forget(hat);
        }
    }
}

void PlayLevelMode::stepWearer(human::Human& human, std::optional<int> type, const human::PlayerCharacter& character,
                               const anim::Pose& pose, anim::Vec3 feet, float heading) {
    Wearer& wearer = m_wearers[&human];
    human::ScriptState& script = human.script();
    const characters::CharacterType* classRecord =
        type ? m_types.find(characters::characterClassOf(*type).id) : nullptr;
    // When he is made (`Human_AttachInstance`), his class's hat, unless it is none or a script gave him one.
    if (!wearer.dressed) {
        wearer.dressed = true;
        if (classRecord != nullptr && !classRecord->hat.empty() && classRecord->hat != world_objects::kNoHat &&
            script.hat == 0.0 && m_cast.objectHandles) {
            script.hat = m_cast.objectHandles();
            script.hatName = classRecord->hat;
        }
    }
    // A new hat (a script's) knocks off the one he wore.
    if (wearer.hat != 0.0 && script.hat != wearer.hat) {
        knockOffHat(wearer, heading);
    }
    // The new hat is made at his head and worn there.
    if (script.hat != 0.0 && wearer.hat == 0.0) {
        putOnHat(human, type, classRecord, script.hat, script.hatName, wearer);
        if (wearer.hat == 0.0) {
            script.hat = 0.0;
            script.hatName.clear();
        }
    }
    // A hat whose object is gone is no longer worn.
    if (wearer.hat != 0.0) {
        const world_objects::SpawnRecord* record = m_records->find(wearer.hat);
        if (record == nullptr || record->removed) {
            m_wornHats.erase(wearer.hat);
            wearer.hat = 0.0;
            script.hat = 0.0;
            script.hatName.clear();
        }
    }
    // Where the hat is this step, for its record and a knock-off.
    if (wearer.hat != 0.0) {
        const auto bones = anim::boneTransforms(character.skeleton(), pose);
        wearer.pose =
            world_objects::heldWorldPose(feet, heading, bones.at(world_objects::kHatBone), 1.0F, wearer.local);
        if (world_objects::SpawnRecord* record = m_records->find(wearer.hat); record != nullptr) {
            record->position = {wearer.pose.position.x, wearer.pose.position.y, wearer.pose.position.z};
            record->rotation = {wearer.pose.rotation.x, wearer.pose.rotation.y, wearer.pose.rotation.z,
                                wearer.pose.rotation.w};
        }
    }
    // Going down knocks it off: a knockdown (but for a cop or class 0x80 or 13), being knocked out, being mounted.
    // **Coney's reading**: the reactions and messages that call the knock-off are read as the human's state changing
    // to grounded or mounted.
    const human::TargetState state = human.state();
    const bool wasUp = wearer.state == human::TargetState::Standing || wearer.state == human::TargetState::Held;
    bool knocked = false;
    if (wasUp && state == human::TargetState::Grounded) {
        const int classId = type ? characters::characterClassOf(*type).id : 0;
        const bool cop = classRecord != nullptr && classRecord->behaviour &&
                         ai::brainTypeOf(*classRecord->behaviour) == ai::BrainType::Cop;
        const bool keeps = cop || classId == kKeepsHatClassA || classId == kKeepsHatClassB;
        knocked = human.health().value() <= 0 || !keeps;
    } else if (wearer.state != human::TargetState::Mounted && state == human::TargetState::Mounted) {
        knocked = true;
    }
    wearer.state = state;
    const world_objects::ObjectType* hatType = wearer.hat != 0.0 ? m_objectTypes->find(script.hatName) : nullptr;
    const bool staysOn = human.hasFlag(human::flag::kKeepHat) ||
                         (hatType != nullptr && hatType->objectKind == world_objects::kHatKindStaysOn);
    if (knocked && wearer.hat != 0.0 && !staysOn) {
        knockOffHat(wearer, heading);
        script.hat = 0.0;
        script.hatName.clear();
    }
}

void PlayLevelMode::putOnHat(const human::Human& human, std::optional<int> type,
                             const characters::CharacterType* classRecord, double hat, const std::string& hatName,
                             Wearer& wearer) {
    const world_objects::ObjectType* hatType = m_objectTypes->find(hatName);
    if (hatType == nullptr) {
        m_print(std::format("hats: no object type {}; human not given hat {}\n", hatName, hat));
        return;
    }
    // Its object: a record of the hat's handle, pinned in the world (Human_SpawnHat), unless one is there already.
    if (m_records->find(hat) == nullptr) {
        world_objects::SpawnRecord record;
        record.handle = hat;
        record.typeName = hatName;
        const anim::Vec3 at = human.position();
        record.position = {at.x, at.y, at.z};
        record.live = true;
        record.pinned = true;
        if (m_records->add(std::move(record)) == nullptr) {
            m_print(std::format("hats: no room for hat {} ({})\n", hat, hatName));
            return;
        }
    }
    // Worn on the head at a Warrior's fitting, else at the hat's own held pose.
    const bool warrior = classRecord != nullptr && classRecord->behaviour &&
                         ai::brainTypeOf(*classRecord->behaviour) == ai::BrainType::Warrior;
    const int classId = type ? characters::characterClassOf(*type).id : 0;
    wearer.local = world_objects::hatAttachment(
        m_hatFits, warrior, type.value_or(0), classId, hatName,
        world_objects::HatFit{.offset = hatType->holdPosition, .rotation = hatType->holdRotation}, human.scale());
    wearer.hat = hat;
    m_wornHats.insert(hat);
    const anim::Vec3 at = human.position();
    m_print(std::format("hats: {} worn as object {} by the human at ({:.2f}, {:.2f}, {:.2f})\n", hatName, hat, at.x,
                        at.y, at.z));
}

void PlayLevelMode::knockOffHat(Wearer& wearer, float heading) {
    m_wornHats.erase(wearer.hat);
    m_fallingHats.knockOff(wearer.hat, wearer.pose, heading, world_objects::hatThrow(kKnockSide));
    wearer.hat = 0.0;
}

void PlayLevelMode::removeWornHat(human::Human& human) {
    const auto found = m_wearers.find(&human);
    if (found == m_wearers.end() || found->second.hat == 0.0) {
        return;
    }
    // **Coney's stand-in**: a deleted human's hat goes with him (the hat's update ends once its wearer is gone).
    const double hat = std::exchange(found->second.hat, 0.0);
    m_wornHats.erase(hat);
    static_cast<void>(m_records->destroy(hat));
    human.script().hat = 0.0;
    human.script().hatName.clear();
}

void PlayLevelMode::takeHatOf(const human::Human& from) {
    human::Human& player = m_player->human();
    removeWornHat(player);
    const auto found = m_wearers.find(&from);
    if (found == m_wearers.end()) {
        return;
    }
    // He becomes the wearer, the human he replaces is forgotten (his hat is not removed with him).
    Wearer moved = found->second;
    m_wearers.erase(found);
    player.script().hat = moved.hat;
    player.script().hatName = from.script().hatName;
    moved.state = player.state();
    m_wearers[&player] = moved;
}

void PlayLevelMode::poseHat(const human::Human& human, const human::PlayerCharacter& character, const anim::Pose& pose,
                            anim::Vec3 feet, float heading) {
    const auto found = m_wearers.find(&human);
    if (found == m_wearers.end() || found->second.hat == 0.0) {
        return;
    }
    const auto bones = anim::boneTransforms(character.skeleton(), pose);
    m_hatDraws.emplace_back(
        found->second.hat,
        world_objects::heldWorldPose(feet, heading, bones.at(world_objects::kHatBone), 1.0F, found->second.local));
}

void PlayLevelMode::drawHats() {
    for (const auto& [hat, pose] : m_hatDraws) {
        const world_objects::SpawnRecord* record = m_records != nullptr ? m_records->find(hat) : nullptr;
        const world_objects::ObjectType* type =
            record != nullptr && m_objectTypes != nullptr ? m_objectTypes->find(record->typeName) : nullptr;
        if (type != nullptr && !record->hidden) {
            m_placed->place(hat, type->modelHash, pose.position, pose.rotation,
                            PlacedObjects::Look{.tint = record->tint, .sizeCullExempt = true});
        }
    }
}

} // namespace coney::platform
