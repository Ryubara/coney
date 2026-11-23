// SPDX-License-Identifier: GPL-3.0-or-later
// What the level simulation's humans hold: the objects a script puts in a hand made as world objects
// (docs/research/objects.md#held), and where an object hangs in a human's hand, posed by his own hand bone as the
// pick-up clip's take event places it (docs/research/objects.md#given-weapon), for a drop's start and for the
// platform's drawing of it.
#include <cstddef>
#include <format>
#include <optional>
#include <utility>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/skeleton.h"
#include "gamemodes/level_pickups.h"
#include "gamemodes/play_level_simulation.h"
#include "human/carried_prop.h"
#include "human/fighter.h"
#include "human/human.h"
#include "world_objects/object_types.h"
#include "world_objects/pickups.h"
#include "world_objects/spawn_records.h"

namespace coney {

std::optional<world_objects::WorldPose> PlayLevelSimulation::heldPose(double held,
                                                                      const human::PlayerSnapshot& snapshot) const {
    const std::optional<HeldDraw> draw =
        heldDrawOf(held, m_player->human(), playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading);
    return draw ? std::optional<world_objects::WorldPose>(draw->pose) : std::nullopt;
}

std::optional<world_objects::WorldPose> PlayLevelSimulation::handPose(const human::Human& human, double held) const {
    if (&human == &m_player->human()) {
        return heldPose(held, m_player->current());
    }
    // An AI human at his last step's pose, with his own character's clips.
    for (const ai::AiHuman& fighter : m_ai->humans()) {
        if (fighter.human.get() != &human) {
            continue;
        }
        const auto played = m_played.find(&human);
        if (played == m_played.end() || played->second.character == nullptr) {
            return std::nullopt;
        }
        const std::optional<HeldDraw> draw = heldDrawOf(held, human, *played->second.character, fighter.current.pose,
                                                        fighter.current.feet, fighter.current.heading);
        return draw ? std::optional<world_objects::WorldPose>(draw->pose) : std::nullopt;
    }
    return std::nullopt;
}

// @orig 0x0024c6d8 Human_PlaceObjectInHandByPickupClip (unknown)
// @orig 0x003a19b0 Obj_GetWorldPose (unknown)
std::optional<PlayLevelSimulation::HeldDraw> PlayLevelSimulation::heldDrawOf(double held, const human::Human& human,
                                                                             const human::PlayerCharacter& character,
                                                                             const anim::Pose& pose, anim::Vec3 feet,
                                                                             float heading) const {
    // The pick-up clip's take event in his own animations gives the bone and the local pose, at his scale, as the
    // swap and HuPlaceItemInHand place it for any human (docs/research/objects.md#given-weapon), or for a two-handed
    // class the type's held pose at that bone (docs/research/objects.md#two-handed); composed with the bone of his
    // pose and the body's placement (the lean left out). **Coney's reading**: the bone's position is not scaled by the
    // human's scale, as Coney draws the body unscaled. **Coney's stand-in**: the take is taken as out of a scene
    // (Human_IsInSceneState at the take is not kept), so a two-handed object always has its type's held pose.
    const world_objects::SpawnRecord* record =
        held != world_objects::kNoObject && m_records != nullptr ? m_records->find(held) : nullptr;
    const world_objects::ObjectType* type =
        record != nullptr && m_objectTypes != nullptr ? m_objectTypes->find(record->typeName) : nullptr;
    const anim::AnimClip* clip =
        type != nullptr
            ? character.anims().clip(static_cast<std::size_t>(world_objects::pickupClip(type->pickupAnim, 0.0F)))
            : nullptr;
    const std::optional<world_objects::HeldAttachment> attachment =
        clip != nullptr ? world_objects::takeAttachment(*clip, human.scale(), *type, false) : std::nullopt;
    if (!attachment || attachment->bone >= anim::kPoseBones) {
        return std::nullopt;
    }
    const auto bones = anim::boneTransforms(character.skeleton(), pose);
    return HeldDraw{.holder = &human,
                    .handle = held,
                    .bone = attachment->bone,
                    .local = *attachment,
                    .pose = world_objects::heldWorldPose(feet, heading, bones.at(attachment->bone), 1.0F, *attachment)};
}

void PlayLevelSimulation::stepPlacedHeld() {
    if (m_records == nullptr || m_objectTypes == nullptr || m_pickups == nullptr) {
        return;
    }
    // Player 1 and each AI human not deleted: a script may arm any of them.
    placeItemInHand(m_player->human(), playerHandle());
    const std::vector<ai::AiHuman>& fighters = m_ai->humans();
    for (std::size_t i = 0; i < fighters.size(); ++i) {
        if (!fighters[i].removed) {
            placeItemInHand(*fighters[i].human, castHandleOf(fighters[i]));
        }
    }
}

void PlayLevelSimulation::placeItemInHand(human::Human& human, double holder) {
    // The binding (Human_PlaceItemInHand's caller 0x00238540, ScriptedHumans::placeItemInHand) gave him the handle and
    // the type's name, and refused a human with something in hand, so there is nothing to drop here. A handle that is
    // a record already came from a pick-up, a swap or a pile, and is made.
    human::ScriptState& script = human.script();
    const double held = script.heldObject;
    if (held == world_objects::kNoObject || m_records->find(held) != nullptr) {
        return;
    }
    // Made by name: a type the object list lacks makes nothing, and he holds nothing.
    const world_objects::ObjectType* type = m_objectTypes->find(script.heldObjectName);
    if (type == nullptr) {
        m_print(std::format("held: no object type {}; human not given object {:.0f}\n", script.heldObjectName, held));
        script.heldObject = world_objects::kNoObject;
        script.heldObjectName.clear();
        return;
    }
    // A type of pick-up kind 5 is carried as a prop at the human's attach point (Human_CreateCarriedProp), never in the
    // hand. **Coney's stand-in**: a placement makes no carried prop yet (four disc calls name such a type), so he holds
    // nothing, as the original's hand stays empty (docs/research/objects.md, its open questions).
    if (type->pickupAnim == human::kCarriedPropKind) {
        m_print(std::format("held: {} is carried as a prop, not made (stand-in)\n", script.heldObjectName));
        script.heldObject = world_objects::kNoObject;
        script.heldObjectName.clear();
        return;
    }
    // Its object at his feet with no turn (the identity at 0x00511790), recorded and pinned; his pick-up clip's take
    // event places it in his hand from there (heldDrawOf()), and it is held as a take holds it. **Coney's choice**: it
    // is made on the step after the binding's call, not in it, and its record stays pinned while held (the original
    // unpins it once attached), as a picked-up object's does.
    world_objects::SpawnRecord record;
    record.handle = held;
    record.typeName = script.heldObjectName;
    const anim::Vec3 at = human.position();
    record.position = {at.x, at.y, at.z};
    record.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
    record.live = true;
    record.pinned = true;
    if (m_records->add(std::move(record)) == nullptr) {
        m_print(std::format("held: no room for object {:.0f} ({})\n", held, script.heldObjectName));
        script.heldObject = world_objects::kNoObject;
        script.heldObjectName.clear();
        return;
    }
    static_cast<void>(m_pickups->placeInHand(held, holder));
    // The take's message 3 to the holder (Human_HandleMessage, the call at 0x002460f4) applies the object's anim set,
    // so he fights as armed from now on (his attack pick, grabs and goals read it), as the swap's receiver does, and
    // carries it in its move style.
    const int set = m_pickups->animSetOf(script.heldObjectName);
    human.takeHeldStyle(set);
    human.fighter().setAnimSet(set);
    m_print(std::format("held: {} made as object {:.0f} in the hand of the human at ({:.2f}, {:.2f}, {:.2f})\n",
                        script.heldObjectName, held, at.x, at.y, at.z));
}

std::vector<PlayLevelSimulation::HeldDraw> PlayLevelSimulation::heldDraws() const {
    // Player 1 and each AI human not deleted, at the newest step's pose, skinned as render() skins them.
    std::vector<HeldDraw> draws;
    const human::PlayerSnapshot& player = m_player->current();
    if (std::optional<HeldDraw> draw = heldDrawOf(m_player->human().script().heldObject, m_player->human(),
                                                  playerCharacter(), player.pose, player.feet, player.heading)) {
        draws.push_back(*draw);
    }
    const std::vector<ai::AiHuman>& fighters = m_ai->humans();
    for (std::size_t i = 0; i < fighters.size(); ++i) {
        const auto played = m_played.find(fighters[i].human.get());
        const human::PlayerCharacter* character = played != m_played.end() ? played->second.character : nullptr;
        if (fighters[i].removed || character == nullptr) {
            continue;
        }
        const human::Human& human = *fighters[i].human;
        const human::TargetSnapshot& now = fighters[i].current;
        if (std::optional<HeldDraw> draw =
                heldDrawOf(human.script().heldObject, human, *character, now.pose, now.feet, now.heading)) {
            draws.push_back(*draw);
        }
    }
    return draws;
}

} // namespace coney
