// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/object_tasks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "core/name_hash.h"

namespace coney::world_objects {

namespace {

// The rotation by `angle` radians about the game's z axis (up).
anim::Quat turnAboutZ(float angle) { return anim::Quat{0.0F, 0.0F, std::sin(angle / 2.0F), std::cos(angle / 2.0F)}; }

// The quaternion product a·b: b's rotation, then a's.
anim::Quat multiply(anim::Quat a, anim::Quat b) {
    return anim::Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// The squared distance between two points.
float distanceSq(anim::Vec3 a, anim::Vec3 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// Whether a model hash is one `simple_object` gives `+0x124` = 5, exempting it from the size cull: of the page's 14,
// the eight Wonder Wheel types are named (docs/research/objects.md#simple-object).
bool wonderWheelHash(std::uint32_t hash) {
    static const std::array<std::uint32_t, 8> kHashes{crc32("dyn_s_wwheel_a"),        crc32("dyn_s_wwcart_simple_a"),
                                                      crc32("dyn_s_wwcart_simple_b"), crc32("dyn_s_wwcart_simple_c"),
                                                      crc32("dyn_s_neon_a"),          crc32("dyn_s_neon_b"),
                                                      crc32("dyn_s_neon_c"),          crc32("dyn_s_neon_d")};
    return std::ranges::find(kHashes, hash) != kHashes.end();
}

} // namespace

std::uint32_t columnColourFor(std::uint32_t discModelHash) {
    switch (discModelHash) {
    case 0x27af4fe0U: // dyn_w_mission
    case 0x39cbfb46U: // dyn_objective_yellow
    case 0x646520baU: // dyn_throwtarget
        return 0xC1A04700U;
    case 0xa83a74daU: // dyn_w_goto
    case 0xebef30bbU: // dyn_objective_w
    case 0x34af4687U: // dyn_objective_red
        return 0x99121300U;
    case 0x14dc9db8U: // dyn_w_bonus
    case 0x7c280227U: // dyn_objective_green
        return 0x5F447000U;
    default:
        return 0xFFFFFF00U;
    }
}

bool ObjectiveMarker::update() {
    // WorldObject_Update: +0xcc into +0xc8, the integration of the angular velocity, then the class's update.
    m_drawnAlpha = m_alpha;
    const float turn = 2.0F * std::numbers::pi_v<float>;
    m_angle = std::fmod(m_angle + kTurnPerSecond * kUpdateSeconds, turn);
    const int target = m_shown ? 255 : 0;
    const int alpha = m_alpha;
    m_alpha = static_cast<std::uint8_t>(alpha < target ? std::min(target, alpha + kFadeStep)
                                                       : std::max(target, alpha - kFadeStep));
    return m_dying && m_alpha == 0;
}

float sizeFade(float radius, float distanceSq) {
    if (distanceSq <= 0.0F) {
        return 1.0F;
    }
    const float ratio = radius / distanceSq;
    return std::clamp((ratio - 0.0004F) / 0.0001F, 0.0F, 1.0F);
}

float showDistanceFade(float fadeDistance, float cameraDistance) {
    if (fadeDistance <= 0.0F || cameraDistance <= fadeDistance - 2.0F) {
        return 1.0F;
    }
    return std::max(0.0F, (fadeDistance - cameraDistance) / 2.0F);
}

float appearFade(std::uint32_t msSinceAppeared) {
    return std::min(1.0F, static_cast<float>(msSinceAppeared) / 1000.0F);
}

std::uint32_t scaleTintAlpha(std::uint32_t tint, float factor) {
    const float alpha = static_cast<float>(tint & 0xFFU) * std::clamp(factor, 0.0F, 1.0F);
    return (tint & 0xFFFFFF00U) | static_cast<std::uint32_t>(std::lround(alpha));
}

std::optional<HeldAttachment> heldAttachment(const anim::AnimClip& clip, float humanScale, float grip) {
    for (const anim::ClipEvent& event : clip.events) {
        if (event.type != kTakeEvent && event.type != kTakeEventLeft) {
            continue;
        }
        // The event's bone and pose, its position scaled by the human; then the grip slide along the object's y.
        HeldAttachment held;
        held.bone = event.word;
        held.rotation = event.rotation;
        const anim::Mat34 turn = anim::matrixFromQuat(event.rotation);
        const anim::Vec3 slide = anim::transformDirection(turn, anim::Vec3{0.0F, grip, 0.0F});
        held.position = anim::add(anim::scale(event.position, humanScale), slide);
        return held;
    }
    return std::nullopt;
}

WorldPose heldWorldPose(anim::Vec3 feet, float heading, const anim::Mat34& bone, float boneScale,
                        const HeldAttachment& held) {
    // The bone in the human's frame: its rotation, its position scaled; then the local pose after it.
    const anim::Quat boneTurn = anim::quatFromMatrix(bone);
    const anim::Vec3 inHuman = anim::add(anim::scale(bone.t, boneScale),
                                         anim::transformDirection(anim::matrixFromQuat(boneTurn), held.position));
    // Then the human's transform: turned by the heading about z, moved to the feet.
    const anim::Quat human = turnAboutZ(heading);
    const anim::Vec3 world = anim::add(feet, anim::transformDirection(anim::matrixFromQuat(human), inHuman));
    return WorldPose{world, multiply(human, multiply(boneTurn, held.rotation))};
}

bool ObjectTasks::wanted(const SpawnRecords& records, const SpawnRecord& record, const StepView& view,
                         bool present) const {
    if (record.removed) {
        return false;
    }
    // A record a binding resolved or a scene holds keeps its object (Coney's stand-in, the class comment).
    if (record.live || record.pinned) {
        return true;
    }
    if (!records.zoneEnabled(record.zone)) {
        return false;
    }
    const float d2 = distanceSq(anim::Vec3{record.position[0], record.position[1], record.position[2]}, view.camera);
    if (!present) {
        return d2 < kSpawnDistance * kSpawnDistance;
    }
    const float out = view.drawDistance + kStoreMargin;
    return d2 <= out * out;
}

void ObjectTasks::step(SpawnRecords& records, const ObjectTypes& types, const StepView& view) {
    // In and out: every record's object as streaming wants it.
    std::map<double, Task> kept;
    for (const SpawnRecord& record : records.all()) {
        const ObjectType* type = types.find(record.typeName);
        const auto found = m_tasks.find(record.handle);
        if (type == nullptr || !wanted(records, record, view, found != m_tasks.end())) {
            continue;
        }
        Task task = found != m_tasks.end() ? found->second : Task{};
        if (!task.marker && type->className == kObjectiveClass) {
            task.marker.emplace();
        }
        kept.emplace(record.handle, task);
    }
    m_tasks = std::move(kept);

    // The updates: a marker follows its last show/hide and destroy messages; a dying one at alpha 0 goes.
    std::vector<double> gone;
    for (auto& [handle, task] : m_tasks) {
        task.msSinceAppeared += view.elapsedMs;
        if (!task.marker) {
            continue;
        }
        const SpawnRecord* record = records.find(handle);
        if (record != nullptr && record->dying) {
            task.marker->setDying();
        } else if (record != nullptr) {
            task.marker->setShown(record->shownMessage.value_or(false));
        }
        if (task.marker->update()) {
            gone.push_back(handle);
        }
    }
    for (const double handle : gone) {
        static_cast<void>(records.destroy(handle));
        m_tasks.erase(handle);
    }

    // The draws, in the records' order.
    m_draws.clear();
    for (const SpawnRecord& record : records.all()) {
        const auto found = m_tasks.find(record.handle);
        const ObjectType* type = types.find(record.typeName);
        if (found == m_tasks.end() || type == nullptr) {
            continue;
        }
        if (view.inHand && view.inHand(record.handle)) {
            continue;
        }
        addDraws(record, *type, found->second);
    }
}

void ObjectTasks::addDraws(const SpawnRecord& record, const ObjectType& type, const Task& task) {
    ObjectDraw draw;
    draw.handle = record.handle;
    draw.modelHash = record.model.value_or(type.modelHash);
    draw.position = anim::Vec3{record.position[0], record.position[1], record.position[2]};
    draw.rotation = anim::Quat{record.rotation[0], record.rotation[1], record.rotation[2], record.rotation[3]};
    draw.fadeDistance = record.fadeInDistance;
    if (!task.marker) {
        // A plain object: its record's tint, faded in over its first second; hidden by ObjHide (Coney's reading).
        if (record.hidden) {
            return;
        }
        draw.tint = scaleTintAlpha(record.tint, appearFade(task.msSinceAppeared));
        draw.sizeCullExempt = wonderWheelHash(type.modelHash);
        m_draws.push_back(draw);
        return;
    }
    // The disc: turned about the vertical, its own tint with the marker's alpha, never size-culled (`+0x124` = 1).
    const ObjectiveMarker& marker = *task.marker;
    const anim::Quat disc = multiply(turnAboutZ(marker.angle()), draw.rotation);
    draw.rotation = disc;
    draw.tint = (record.tint & 0xFFFFFF00U) | marker.drawnAlpha();
    draw.sizeCullExempt = true;
    m_draws.push_back(draw);
    // The column: attached at the disc's position, its own turn backwards so it keeps its world orientation, coloured
    // by the disc's type with the same fade.
    ObjectDraw column = draw;
    column.modelHash = kColumnModelHash;
    column.rotation = multiply(disc, turnAboutZ(-marker.angle()));
    column.tint = columnColourFor(type.modelHash) | marker.drawnAlpha();
    column.sizeCullExempt = false;
    column.column = true;
    m_draws.push_back(column);
}

const ObjectiveMarker* ObjectTasks::marker(double handle) const {
    const auto found = m_tasks.find(handle);
    if (found == m_tasks.end()) {
        return nullptr;
    }
    const std::optional<ObjectiveMarker>& marker = found->second.marker;
    return marker ? &*marker : nullptr;
}

} // namespace coney::world_objects
