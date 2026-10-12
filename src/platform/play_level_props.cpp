// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's carried props: the object a human's clip makes him carry for a move (Rembrandt's spray can in his
// power move 59) made when his clip asks for it, hung from its attach point each step, drawn there, and removed when
// the clip, a cinematic's start or his removal takes it away (docs/research/combat.md#power-move-props).
#include <format>
#include <optional>
#include <utility>

#include "animation/skeleton.h"
#include "human/carried_prop.h"
#include "human/human.h"
#include "platform/play_level_mode.h"
#include "world_objects/object_tasks.h"

namespace coney::platform {

void PlayLevelMode::stepCarriedProps() {
    if (m_records == nullptr || m_objectTypes == nullptr) {
        return;
    }
    const human::PlayerSnapshot& player = m_player->current();
    stepCarrier(m_player->human(), playerCharacter(), player.pose, player.feet, player.heading);
    const std::vector<ai::AiHuman>& fighters = m_ai->humans();
    for (std::size_t i = 0; i < fighters.size(); ++i) {
        human::Human& human = *fighters[i].human;
        // A removed human's prop goes with him.
        if (fighters[i].removed) {
            if (const auto found = m_carriers.find(&human); found != m_carriers.end()) {
                removeCarriedProp(found->second);
            }
            continue;
        }
        const human::PlayerCharacter& character = i < m_fighterMeshes.size() && m_fighterMeshes[i].character != nullptr
                                                      ? *m_fighterMeshes[i].character
                                                      : *m_character;
        const human::TargetSnapshot& now = fighters[i].current;
        stepCarrier(human, character, now.pose, now.feet, now.heading);
    }
}

void PlayLevelMode::stepCarrier(human::Human& human, const human::PlayerCharacter& character, const anim::Pose& pose,
                                anim::Vec3 feet, float heading) {
    const std::optional<human::CarriedProp>& wanted = human.carriedProp();
    const auto found = m_carriers.find(&human);
    if (found == m_carriers.end() && !wanted) {
        return;
    }
    Carrier& carrier = m_carriers[&human];
    // A prop the human no longer carries (or one of another type) is removed.
    if (carrier.prop != 0.0 && (!wanted || wanted->typeName != carrier.typeName)) {
        removeCarriedProp(carrier);
    }
    if (!wanted) {
        return;
    }
    if (carrier.prop == 0.0) {
        makeCarriedProp(human, *wanted, carrier);
        if (carrier.prop == 0.0) {
            return;
        }
    }
    // Where it hangs: event 0x37's attach point and pose once one came, else the type's offset at point 0x13.
    carrier.local.bone = wanted->point;
    if (wanted->place) {
        carrier.local.position = wanted->place->position;
        carrier.local.rotation = wanted->place->rotation;
    }
    const auto bones = anim::boneTransforms(character.skeleton(), pose);
    if (carrier.local.bone >= bones.size()) {
        return;
    }
    const world_objects::WorldPose at =
        world_objects::heldWorldPose(feet, heading, bones.at(carrier.local.bone), 1.0F, carrier.local);
    if (world_objects::SpawnRecord* record = m_records->find(carrier.prop); record != nullptr) {
        record->position = {at.position.x, at.position.y, at.position.z};
        record->rotation = {at.rotation.x, at.rotation.y, at.rotation.z, at.rotation.w};
    }
}

void PlayLevelMode::makeCarriedProp(human::Human& human, const human::CarriedProp& prop, Carrier& carrier) {
    // The type the event names, which must be one a human carries (kind 5). **Coney's stand-in**: a type of another
    // kind is not made (the original ignores kind 6 and puts any other kind in the hand, which no clip on the disc is
    // known to ask for), so the human carries nothing.
    const world_objects::ObjectType* type = m_objectTypes->find(prop.typeName);
    if (type == nullptr || type->pickupAnim != human::kCarriedPropKind || !m_cast.objectHandles) {
        m_print(std::format("props: {} not carried ({})\n", prop.typeName,
                            type == nullptr ? "no such type" : "not a kind-5 type"));
        human.releaseCarriedProps();
        return;
    }
    // Its object at the human, pinned (the human, not the streaming, keeps it).
    world_objects::SpawnRecord record;
    record.handle = m_cast.objectHandles();
    record.typeName = prop.typeName;
    const anim::Vec3 at = human.position();
    record.position = {at.x, at.y, at.z};
    record.live = true;
    record.pinned = true;
    const double handle = record.handle;
    if (m_records->add(std::move(record)) == nullptr) {
        m_print(std::format("props: no room for {}\n", prop.typeName));
        human.releaseCarriedProps();
        return;
    }
    // Attached at point 0x13 with the type's own held offset until event 0x37 moves it.
    carrier.prop = handle;
    carrier.typeName = prop.typeName;
    carrier.local = world_objects::HeldAttachment{
        .bone = human::kCarryPoint, .position = type->holdPosition, .rotation = type->holdRotation};
    m_carriedProps.insert(handle);
    m_print(std::format("props: {} carried as object {} by the human at ({:.2f}, {:.2f}, {:.2f})\n", prop.typeName,
                        handle, at.x, at.y, at.z));
}

void PlayLevelMode::removeCarriedProp(Carrier& carrier) {
    if (carrier.prop == 0.0) {
        return;
    }
    const double prop = std::exchange(carrier.prop, 0.0);
    m_carriedProps.erase(prop);
    static_cast<void>(m_records->destroy(prop));
    carrier.typeName.clear();
}

void PlayLevelMode::poseCarriedProp(const human::Human& human, const human::PlayerCharacter& character,
                                    const anim::Pose& pose, anim::Vec3 feet, float heading) {
    const auto found = m_carriers.find(&human);
    if (found == m_carriers.end() || found->second.prop == 0.0) {
        return;
    }
    const auto bones = anim::boneTransforms(character.skeleton(), pose);
    if (found->second.local.bone >= bones.size()) {
        return;
    }
    m_attachedDraws.emplace_back(
        found->second.prop,
        world_objects::heldWorldPose(feet, heading, bones.at(found->second.local.bone), 1.0F, found->second.local));
}

std::optional<double> PlayLevelMode::carriedPropOf(const human::Human& human) const {
    const auto found = m_carriers.find(&human);
    if (found == m_carriers.end() || found->second.prop == 0.0) {
        return std::nullopt;
    }
    return found->second.prop;
}

} // namespace coney::platform
