// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's world objects: the scripts' spawn records brought in round the camera and drawn
// with their models, tints and fades, the objective markers (docs/research/objects.md#objective-markers) and the object
// in player 1's hand (docs/research/objects.md#held).
#include <cstdint>
#include <format>
#include <optional>

#include "animation/anim_clip.h"
#include "animation/skeleton.h"
#include "human/human.h"
#include "platform/play_level_mode.h"
#include "platform/play_lighting.h"
#include "world_objects/object_list.h"
#include "world_objects/pickups.h"

namespace coney::platform {

void PlayLevelMode::makeWorldObjects(const ScriptedCast& cast) {
    m_records = cast.records;
    m_objectTypes = cast.types;
    if (m_records == nullptr || m_objectTypes == nullptr || !m_engine.drawsPixels()) {
        return;
    }
    // The models come by the Object List; without it the objects are stepped but not drawn.
    auto list = world_objects::loadObjectList(m_wad);
    if (!list) {
        m_print(std::format("objects: no Object List, no world objects drawn: {}\n", list.error().message));
        return;
    }
    m_objectList = std::make_unique<world_objects::ObjectList>(std::move(*list));
    m_placed = std::make_unique<PlacedObjects>(m_wad, *m_objectList, m_print, true);
}

void PlayLevelMode::stepWorldObjects(world::Vec3 eye, std::uint32_t elapsedMs) {
    if (m_records == nullptr || m_objectTypes == nullptr) {
        return;
    }
    // RenderWare's axes back into the game's: (x, y, z) is the game's (x, -z, y).
    world_objects::ObjectTasks::StepView view;
    view.camera = anim::Vec3{eye.x, -eye.z, eye.y};
    view.drawDistance = m_drawDistance.current();
    view.elapsedMs = elapsedMs;
    if (m_pickups != nullptr) {
        view.inHand = [this](double handle) { return m_pickups->inHand(handle); };
    }
    m_objectTasks.step(*m_records, *m_objectTypes, view);
}

void PlayLevelMode::drawWorldObjects(const human::PlayerSnapshot& snapshot) {
    if (!m_placed) {
        return;
    }
    m_placed->clear();
    for (const world_objects::ObjectDraw& draw : m_objectTasks.draws()) {
        m_placed->place(draw.handle + (draw.column ? 0.5 : 0.0), draw.modelHash, draw.position, draw.rotation,
                        PlacedObjects::Look{.tint = draw.tint,
                                            .fadeDistance = draw.fadeDistance,
                                            .sizeCullExempt = draw.sizeCullExempt,
                                            .translucent = draw.column});
    }
    // The object in player 1's hand (docs/research/objects.md#held): the pick-up clip's take event gives the bone and
    // the local pose, composed with the bone of this frame's pose and the body's placement (the lean left out).
    // **Coney's reading**: the bone's position is not scaled by the human's scale, as Coney draws the body unscaled.
    const double held = m_player->human().script().heldObject;
    const world_objects::SpawnRecord* record =
        held != world_objects::kNoObject && m_records != nullptr ? m_records->find(held) : nullptr;
    const world_objects::ObjectType* type =
        record != nullptr && m_objectTypes != nullptr ? m_objectTypes->find(record->typeName) : nullptr;
    const human::PlayerCharacter& character = playerCharacter();
    const anim::AnimClip* clip =
        type != nullptr
            ? character.anims().clip(static_cast<std::size_t>(world_objects::pickupClip(type->pickupAnim, 0.0F)))
            : nullptr;
    const std::optional<world_objects::HeldAttachment> attachment =
        clip != nullptr ? world_objects::heldAttachment(*clip, m_player->human().scale(), type->grip) : std::nullopt;
    if (attachment && attachment->bone < anim::kPoseBones) {
        const auto bones = anim::boneTransforms(character.skeleton(), snapshot.pose);
        const world_objects::WorldPose pose = world_objects::heldWorldPose(
            snapshot.feet, snapshot.heading, bones.at(attachment->bone), 1.0F, *attachment);
        m_placed->place(held, type->modelHash, pose.position, pose.rotation,
                        PlacedObjects::Look{.tint = record->tint, .sizeCullExempt = true});
    }
    PlacedObjects::DrawOptions options;
    const world::Vec3 eye = m_lights->scene().pose().position;
    options.camera = anim::Vec3{eye.x, eye.y, eye.z};
    options.render = [this](rw::Atomic* atomic) { m_lights->drawObject(atomic); };
    m_placed->draw(options);
}

} // namespace coney::platform
