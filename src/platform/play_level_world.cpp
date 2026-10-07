// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's world objects and health rings: the scripts' spawn records brought in round the camera and drawn
// with their models, tints and fades, the objective markers (docs/research/objects.md#objective-markers), the object
// in player 1's hand, the glints of the pickups and lock-pickable doors (docs/research/particles.md#glints), and the
// rings under player 1 and his target (docs/research/hud.md#the-health-rings).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/skeleton.h"
#include "core/name_hash.h"
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
#include "world_objects/spinning_icons.h"

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

// The first id of the spinning icons among the placed objects: script handles are positive.
constexpr double kIconIdBase = -1.0;

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

void PlayLevelMode::stepWorldObjects(world::Vec3 eye, const WorldView& worldView, std::uint32_t elapsedMs) {
    if (m_records == nullptr || m_objectTypes == nullptr) {
        return;
    }
    // RenderWare's axes back into the game's: (x, y, z) is the game's (x, -z, y).
    world_objects::ObjectTasks::StepView view;
    view.camera = anim::Vec3{eye.x, -eye.z, eye.y};
    view.drawDistance = m_drawDistance.current();
    view.elapsedMs = elapsedMs;
    // An object in a hand or a hat on a head is drawn by its holder.
    view.inHand = [this](double handle) {
        return m_wornHats.contains(handle) || (m_pickups != nullptr && m_pickups->inHand(handle));
    };
    m_objectTasks.step(*m_records, *m_objectTypes, view);
    // The glints, at 60 ticks a second of the step's game time.
    stepGlints(worldView, static_cast<int>(std::lround(static_cast<double>(elapsedMs) * 60.0 / 1000.0)));
}

void PlayLevelMode::stepGlints(const WorldView& worldView, int ticks) {
    // The owners: each `pickup_item` lying in the world (in this step's draws, so not in a hand and not hidden), and
    // each door whose lock-pick glint is up.
    std::vector<effects::GlintOwner> owners;
    for (const world_objects::ObjectDraw& draw : m_objectTasks.draws()) {
        const world_objects::SpawnRecord* record = draw.column ? nullptr : m_records->find(draw.handle);
        const world_objects::ObjectType* type = record != nullptr ? m_objectTypes->find(record->typeName) : nullptr;
        if (type != nullptr && type->className == world_objects::kPickupItemClass &&
            !world_objects::neverGlints(type->modelHash)) {
            owners.push_back(effects::GlintOwner{draw.handle, draw.position});
        }
    }
    if (m_objects != nullptr) {
        for (const world_objects::Door& door : m_objects->doors.doors()) {
            if (door.glint && !door.removed && !door.ended) {
                owners.push_back(effects::GlintOwner{door.handle, door.glintAt});
            }
        }
    }
    m_glints.sync(owners);
    // Seen: in front of the camera, inside its view window, within the distance asked (game axes into RenderWare's).
    const auto visible = [&worldView](anim::Vec3 point, float distance) {
        const world::Vec3 at = toRenderWare(point);
        const world::Vec3& eye = worldView.pose.position;
        const world::Vec3 d{at.x - eye.x, at.y - eye.y, at.z - eye.z};
        const float ahead =
            d.x * worldView.pose.forward.x + d.y * worldView.pose.forward.y + d.z * worldView.pose.forward.z;
        if (ahead <= 0.0F || d.x * d.x + d.y * d.y + d.z * d.z > distance * distance) {
            return false;
        }
        const float across = d.x * worldView.pose.right.x + d.y * worldView.pose.right.y + d.z * worldView.pose.right.z;
        const float upward = d.x * worldView.pose.up.x + d.y * worldView.pose.up.y + d.z * worldView.pose.up.z;
        return std::fabs(across) <= ahead * worldView.halfWidth && std::fabs(upward) <= ahead * worldView.halfHeight;
    };
    m_glints.tick(ticks, visible);
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
    // The spinning icons over the humans that wear one (a dealer's, a script's `HuAttachSpinningIcon`), turning with
    // the game time of the newest step (docs/research/ai.md#dealer-icon). Their ids are negative, apart from the
    // handles.
    double iconId = kIconIdBase;
    const bool letterbox = m_stage->barHeight(m_hud->hud().nowMs()) > 0.0F;
    for (const human::Human* human : m_player->humans().humans()) {
        const std::string& icon = human->script().icon;
        // Hidden while the letterbox is up (all but dyn_cross).
        if (icon.empty() || !human->alive() || (letterbox && world_objects::hiddenByLetterbox(icon))) {
            continue;
        }
        const world_objects::IconPose pose =
            world_objects::spinningIconPose(icon, human->position(), human->heading(), m_hud->hud().nowMs());
        m_placed->place(iconId, crc32(icon), pose.position, pose.rotation, PlacedObjects::Look{.sizeCullExempt = true});
        iconId -= 1.0;
    }
    drawHats();
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
