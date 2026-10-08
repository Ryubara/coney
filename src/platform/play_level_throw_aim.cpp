// SPDX-License-Identifier: GPL-3.0-or-later
// Player 1's throw aim in the level (docs/research/objects.md#throws): L1 with a set 5 object in hand enters it, the
// left stick turns and pitches it, the arc is traced through the level's humans, world objects and mesh each frame,
// and L1 again, a lost object or a grab leaves it. The aim itself is combat::ThrowAimState's.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <vector>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/throw_aim.h"
#include "combat/throw_velocity.h"
#include "platform/play_level_mode.h"
#include "raycast/collision_mesh.h"
#include "world_objects/object_bodies.h"

namespace coney::platform {

namespace {

// The anim set whose objects (bottles, bricks, balls) may be aimed (docs/research/objects.md#throws).
constexpr int kAimSet = 5;
// **Coney's stand-ins** for the physics sweep (`IPhysics_CollideShape`, not on the page in this detail): the sweep is
// sampled every this many metres along a segment, and a human's body is his 2 m upright capsule on his scale.
constexpr float kSweepStep = 0.1F;
constexpr float kHumanBodyHeight = 2.0F;
// **Coney's stand-in** for a human target's point (his head bone, posed): this height on his scale above his feet,
// as the plain throw's.
constexpr float kAimTargetHeight = 1.6F;

// Whether `animId` is one of set 5's throws (467 standing, 471 walking, 472 running), which a throw from the aim plays.
bool aimThrowClip(std::uint32_t animId) {
    constexpr std::array<int, 3> kThrows{combat::anim_id::kOneHandedThrow, combat::anim_id::kOneHandedThrowFromWalk,
                                         combat::anim_id::kOneHandedThrowFromRun};
    return std::ranges::find(kThrows, static_cast<int>(animId)) != kThrows.end();
}

// Whether a sphere at `at` of `radius` touches `human`'s upright capsule.
bool touchesHuman(const human::Human& human, anim::Vec3 at, float radius) {
    const anim::Vec3 feet = human.position();
    const float reach = human.capsuleRadius() + radius;
    if (std::hypot(at.x - feet.x, at.y - feet.y) > reach) {
        return false;
    }
    return at.z >= feet.z - radius && at.z <= feet.z + (kHumanBodyHeight * human.bodyScale()) + radius;
}

// The first sample along `from`-`to` (every kSweepStep, the end included) for which `meets` holds, as a fraction.
template <typename Meets> std::optional<float> sweep(anim::Vec3 from, anim::Vec3 to, Meets&& meets) {
    const anim::Vec3 along = anim::subtract(to, from);
    const float length = std::sqrt((along.x * along.x) + (along.y * along.y) + (along.z * along.z));
    const int samples = std::max(1, static_cast<int>(std::ceil(length / kSweepStep)));
    for (int i = 0; i <= samples; ++i) {
        const float fraction = static_cast<float>(i) / static_cast<float>(samples);
        if (meets(anim::add(from, anim::scale(along, fraction)))) {
            return fraction;
        }
    }
    return std::nullopt;
}

} // namespace

void PlayLevelMode::stepThrowAim(const Pad& pad) {
    human::Human& human = m_player->human();
    human::ScriptState& script = human.script();
    const world_objects::SpawnRecord* record = m_records != nullptr && script.heldObject != world_objects::kNoObject
                                                   ? m_records->find(script.heldObject)
                                                   : nullptr;
    const world_objects::ObjectType* type =
        record != nullptr && m_objectTypes != nullptr ? m_objectTypes->find(record->typeName) : nullptr;
    const bool aimable = record != nullptr && m_pickups != nullptr &&
                         m_pickups->animSetOf(script.heldObjectName) == kAimSet && m_player->padControlled() &&
                         human.state() == human::TargetState::Standing && human.fighter().held() == nullptr &&
                         !human.airborne();
    const bool l1 = human.record().command == combat::command::kL1Pressed;
    // A throw started from the aim plays out; its release (dropHeld()) leaves the aim.
    if (m_throwAim.active() && aimThrowClip(human.animator().animId())) {
        return;
    }
    // Leaving: L1 again, or the object, the stance or the pad lost (a grab, a mug, an AI in control).
    if (m_throwAim.active() && (l1 || !aimable)) {
        m_throwAim.leave();
        human.setThrowAiming(false);
        m_print("throw: left the aim\n");
        return;
    }
    // Entering: L1 with a set 5 object and no target locked (Player_OnL1Pressed). **Coney's reading**: Coney's lock
    // never takes a friend, so "no target locked, or a friendly one" is no lock at all.
    if (!m_throwAim.active()) {
        if (!l1 || !aimable || human.fighter().lockTarget() != nullptr) {
            return;
        }
        m_throwAim.enter(human.position(), human.heading());
        human.setThrowAiming(true);
        m_print("throw: aiming\n");
    }
    // The frame's input: the raw left stick (HuLockPadMovement centres only the player record), the follow camera's
    // heading for the first frame, the held object's weight and size.
    combat::AimInput input;
    input.stickX = static_cast<int>(pad.rawSticks()[2]) - static_cast<int>(pad::kStickCentre);
    input.stickY = static_cast<int>(pad.rawSticks()[3]) - static_cast<int>(pad::kStickCentre);
    if (const std::optional<anim::Vec3> forward = m_player->followForward();
        forward && (forward->x != 0.0F || forward->y != 0.0F)) {
        input.followHeading = std::atan2(-forward->x, forward->y);
    }
    input.weightFactor = combat::throwWeightFactor(type != nullptr ? type->objectKind : 0,
                                                   type != nullptr ? type->weight : 0, human.scale());
    input.radius = type != nullptr ? type->bodySize[0] / 2.0F : 0.0F;

    // The world the arc is traced through: the level's AI humans, its world objects' bodies and its mesh.
    const ai::Gang* playerGang = m_ai != nullptr ? m_ai->playerBrain().gang() : nullptr;
    const auto aiHumans = [this]() {
        std::vector<const human::Human*> out;
        if (m_ai != nullptr) {
            for (const ai::AiHuman& ai : m_ai->humans()) {
                if (!ai.removed && ai.human != nullptr) {
                    out.push_back(ai.human.get());
                }
            }
        }
        return out;
    };
    const std::vector<const human::Human*> others = aiHumans();
    const anim::Vec3 origin = human.position();
    const float heading = human.heading();
    combat::AimWorld world;
    // Pass 1 (ThrowArc_TargetFilter): an AI human not friendly to the thrower, on the stick's side when it is pushed
    // sideways.
    world.humans = [&](anim::Vec3 from, anim::Vec3 to, float radius, int side) -> std::optional<combat::AimHit> {
        std::optional<combat::AimHit> best;
        for (const human::Human* other : others) {
            const ai::Brain* brain = m_ai->brainOf(*other);
            if (brain == nullptr || brain->dead() ||
                (playerGang != nullptr && ai::Gangs::friends(brain->gang(), playerGang))) {
                continue;
            }
            if (side != 0) {
                const anim::Vec3 at = other->position();
                const float across = ((at.x - origin.x) * std::cos(heading)) + ((at.y - origin.y) * std::sin(heading));
                if ((side > 0) != (across > 0.0F)) {
                    continue;
                }
            }
            const std::optional<float> fraction =
                sweep(from, to, [&](anim::Vec3 at) { return touchesHuman(*other, at, radius); });
            if (fraction && (!best || *fraction < best->fraction)) {
                best = combat::AimHit{.handle = handleOf(*other), .fraction = *fraction};
            }
        }
        return best;
    };
    // Pass 2: the level mesh (handle 0), a world object with THROWNWEAPONTARGET, or any AI human.
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    world.anything = [&](anim::Vec3 from, anim::Vec3 to, float radius) -> std::optional<combat::AimHit> {
        std::optional<combat::AimHit> best;
        const anim::Vec3 along = anim::subtract(to, from);
        const float length = std::sqrt((along.x * along.x) + (along.y * along.y) + (along.z * along.z));
        if (length > 0.0F) {
            const raycast::Ray ray{.origin = raycast::Vec3{from.x, from.y, from.z},
                                   .direction = raycast::Vec3{along.x / length, along.y / length, along.z / length},
                                   .length = length};
            if (const std::optional<raycast::RayHit> hit = mesh.rayCast(ray, {}, 0)) {
                best = combat::AimHit{.handle = 0, .fraction = hit->t / length};
            }
        }
        double object = 0;
        const std::optional<float> objectAt = sweep(from, to, [&](anim::Vec3 at) {
            const std::vector<double> met = m_objectBodies.touching(at, radius, world_objects::kPhyThrownWeaponTarget);
            object = met.empty() ? 0 : met.front();
            return !met.empty();
        });
        if (objectAt && (!best || *objectAt < best->fraction)) {
            best = combat::AimHit{.handle = object, .fraction = *objectAt};
        }
        for (const human::Human* other : others) {
            const std::optional<float> fraction =
                sweep(from, to, [&](anim::Vec3 at) { return touchesHuman(*other, at, radius); });
            if (fraction && (!best || *fraction < best->fraction)) {
                best = combat::AimHit{.handle = handleOf(*other), .fraction = *fraction};
            }
        }
        return best;
    };
    world.targetPoint = [&](double target) -> std::optional<anim::Vec3> {
        for (const human::Human* other : others) {
            if (handleOf(*other) == target) {
                const anim::Vec3 at = other->position();
                return anim::Vec3{at.x, at.y, at.z + (kAimTargetHeight * other->bodyScale())};
            }
        }
        return std::nullopt;
    };

    const double before = m_throwAim.aimed();
    const bool turning = m_throwAim.step(input, world);
    human.aimThrow(m_throwAim.heading(), turning);
    script.aimedObject = m_throwAim.aimed();
    if (m_throwAim.aimed() != before) {
        m_print(std::format("throw: aiming at {:.0f} (pitch {:.3f})\n", m_throwAim.aimed(), m_throwAim.pitch()));
    }
}

} // namespace coney::platform
