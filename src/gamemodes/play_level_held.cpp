// SPDX-License-Identifier: GPL-3.0-or-later
// What the level simulation's humans hold: where an object hangs in a human's hand, posed by his own hand bone as the
// pick-up clip's take event places it (docs/research/objects.md#given-weapon), for a drop's start and for the
// platform's drawing of it.
#include <cstddef>
#include <optional>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/skeleton.h"
#include "gamemodes/play_level_simulation.h"
#include "human/human.h"
#include "world_objects/pickups.h"

namespace coney {

std::optional<world_objects::WorldPose> PlayLevelSimulation::heldPose(double held,
                                                                      const human::PlayerSnapshot& snapshot) const {
    const std::optional<HeldDraw> draw =
        heldDrawOf(held, m_player->human(), playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading);
    return draw ? std::optional<world_objects::WorldPose>(draw->pose) : std::nullopt;
}

// @orig 0x0024c6d8 Human_PlaceObjectInHandByPickupClip (unknown)
// @orig 0x003a19b0 Obj_GetWorldPose (unknown)
std::optional<PlayLevelSimulation::HeldDraw> PlayLevelSimulation::heldDrawOf(double held, const human::Human& human,
                                                                             const human::PlayerCharacter& character,
                                                                             const anim::Pose& pose, anim::Vec3 feet,
                                                                             float heading) const {
    // The pick-up clip's take event in his own animations gives the bone and the local pose, at his scale, as the
    // swap and HuPlaceItemInHand place it for any human (docs/research/objects.md#given-weapon); composed with the
    // bone of his pose and the body's placement (the lean left out). **Coney's reading**: the bone's position is not
    // scaled by the human's scale, as Coney draws the body unscaled.
    const world_objects::SpawnRecord* record =
        held != world_objects::kNoObject && m_records != nullptr ? m_records->find(held) : nullptr;
    const world_objects::ObjectType* type =
        record != nullptr && m_objectTypes != nullptr ? m_objectTypes->find(record->typeName) : nullptr;
    const anim::AnimClip* clip =
        type != nullptr
            ? character.anims().clip(static_cast<std::size_t>(world_objects::pickupClip(type->pickupAnim, 0.0F)))
            : nullptr;
    const std::optional<world_objects::HeldAttachment> attachment =
        clip != nullptr ? world_objects::heldAttachment(*clip, human.scale(), type->grip) : std::nullopt;
    if (!attachment || attachment->bone >= anim::kPoseBones) {
        return std::nullopt;
    }
    const auto bones = anim::boneTransforms(character.skeleton(), pose);
    return HeldDraw{.holder = &human,
                    .handle = held,
                    .bone = attachment->bone,
                    .pose = world_objects::heldWorldPose(feet, heading, bones.at(attachment->bone), 1.0F, *attachment)};
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
