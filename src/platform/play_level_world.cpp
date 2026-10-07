// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's world objects and health rings: the scripts' spawn records brought in round the camera and drawn
// with their models, tints and fades, the objective markers (docs/research/objects.md#objective-markers), the object
// in player 1's hand, and the rings under player 1 and his target (docs/research/hud.md#the-health-rings).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <utility>

#include "animation/anim_clip.h"
#include "animation/skeleton.h"
#include "core/pad.h"
#include "human/combatant.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/target_human.h"
#include "platform/play_level_mode.h"
#include "platform/play_lighting.h"
#include "world_objects/cars.h"
#include "world_objects/doors.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_list.h"
#include "world_objects/pickups.h"

namespace coney::platform {

namespace {

// A pointer as the rings' id of the human it names.
std::uint64_t ringId(const void* human) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(human)); }

// A human's health as the rings take it, percent 0-100.
float healthPercentOf(const combat::Health& health) { return std::clamp(health.fraction() * 100.0F, 0.0F, 100.0F); }

// Whether a human in `state` may show rings: not down (grounded or mounted) or dead.
bool standing(human::TargetState state) {
    return state == human::TargetState::Standing || state == human::TargetState::Held;
}

} // namespace

void PlayLevelMode::makeWorldObjects(const ScriptedCast& cast) {
    m_cast.forceReticules = cast.forceReticules;
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

void PlayLevelMode::stepRings(const Pad& pad, const WorldView& view, std::uint64_t nowMs) {
    const human::Human& player = m_player->human();
    const human::Fighter& fighter = player.fighter();
    // A human's part of the update; its hit pulse's target is the health it lost since the last step (**Coney's
    // reading** of `Human_AddPendingDamage`'s damage).
    const auto ringHuman = [this](const human::Human& human, bool isPlayer) {
        hud::RingHuman ring;
        ring.id = ringId(&human);
        ring.feet = human.position();
        ring.healthPercent = healthPercentOf(human.health());
        ring.powerPercent = std::clamp(human.fighter().combat().power().fraction() * 100.0F, 0.0F, 100.0F);
        const combat::RageMeter& rage = human.fighter().combat().rage();
        ring.rage = rage.value();
        ring.raging = rage.raging();
        ring.rageFull = rage.full();
        ring.classByte = human.fighter().victim().powerClass().ringByte;
        ring.canShow = !human.airborne() && human.traversal() != human::Traversal::Climbing && human.alive() &&
                       standing(human.state());
        ring.player = isPlayer;
        int& last = m_ringHealth[ring.id];
        const int health = human.health().value();
        ring.damageTaken = last > health ? last - health : 0;
        last = health;
        return ring;
    };

    hud::RingFrame frame;
    frame.nowMs = nowMs;
    frame.hudShown = m_hud->hud().visible();
    frame.forceAll = m_cast.forceReticules != nullptr && *m_cast.forceReticules;
    // The heading of the camera's forward in the game's axes (RenderWare's (x, y, z) is the game's (x, -z, y)).
    frame.cameraHeading = std::atan2(view.pose.forward.x, -view.pose.forward.z);
    hud::RingPlayer ringPlayer;
    ringPlayer.human = ringHuman(player, true);
    ringPlayer.human.canShow = ringPlayer.human.canShow && !sceneHoldsPlayer();
    ringPlayer.selectPressed = (pad.pressed() & pad::kSelect) != 0;
    // A flash used since the last update asks for the rings (the panel request the update clears).
    ringPlayer.flashUsed = std::exchange(m_flashRingRequest, false);
    // **Coney's stand-in** for the fight stance (record `+0x00` & 3): locked onto a target or blocking.
    ringPlayer.fightStance = fighter.lockTarget() != nullptr || fighter.blocking();
    ringPlayer.holdingL1 = (pad.buttons() & pad::kL1) != 0;
    if (const human::Combatant* target = fighter.target(); target != nullptr) {
        if (const auto* human = dynamic_cast<const human::Human*>(target); human != nullptr) {
            ringPlayer.target = ringHuman(*human, false);
        } else if (const auto* passive = dynamic_cast<const human::TargetHuman*>(target); passive != nullptr) {
            hud::RingHuman ring;
            ring.id = ringId(passive);
            ring.feet = passive->position();
            ring.healthPercent = healthPercentOf(passive->health());
            ring.classByte = passive->powerClass().ringByte;
            ring.canShow = standing(passive->state()) && passive->health().value() > 0;
            ringPlayer.target = ring;
        }
    }
    frame.players.push_back(ringPlayer);
    m_rings.update(frame);
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
    // The doors' leaves and the barriers, at their poses (docs/research/objects.md#doors).
    if (m_objects != nullptr) {
        for (const world_objects::DoorDraw& door : world_objects::doorDraws(m_objects->doors)) {
            const bool listed = m_objectList != nullptr && m_objectList->findByHash(door.modelHash) != nullptr;
            m_placed->place(door.handle, listed ? door.modelHash : door.fallbackHash, door.position, door.rotation,
                            PlacedObjects::Look{.tint = door.tint});
        }
    }
    // Each car's stereo, until a theft takes it (docs/research/cars.md#windows).
    if (m_cars != nullptr) {
        for (const world_objects::StereoDraw& stereo : world_objects::stereoDraws(*m_cars)) {
            m_placed->place(stereo.handle, stereo.modelHash, stereo.position, stereo.rotation, PlacedObjects::Look{});
        }
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

void PlayLevelMode::drawGlass() const {
    if (m_objects == nullptr || !m_engine.drawsPixels()) {
        return;
    }
    // The camera in the game's axes: RenderWare's (x, y, z) is the game's (x, -z, y).
    const world::Vec3 eye = m_lights->scene().pose().position;
    m_lights->drawGlass(world_objects::glassDraws(m_objects->glass, anim::Vec3{eye.x, -eye.z, eye.y}));
}

void PlayLevelMode::drawRings(const std::map<std::uint64_t, anim::Vec3>& feet) const {
    // Each ring and marker under where its human is drawn this frame, at its own height above the feet.
    std::vector<hud::GroundRing> rings = m_rings.rings();
    for (hud::GroundRing& ring : rings) {
        if (const auto found = feet.find(ring.id); found != feet.end()) {
            ring.centre = anim::Vec3{found->second.x, found->second.y, found->second.z + ring.lift};
        }
    }
    std::vector<hud::TargetMarker> markers = m_rings.markers();
    for (hud::TargetMarker& marker : markers) {
        if (const auto found = feet.find(marker.id); found != feet.end()) {
            marker.centre = anim::Vec3{found->second.x, found->second.y, found->second.z + marker.lift};
        }
    }
    m_lights->drawRings(rings, markers);
}

} // namespace coney::platform
