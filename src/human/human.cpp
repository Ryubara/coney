// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "animation/anim_task.h"
#include "human/body.h"
#include "human/jump.h"

namespace coney::human {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// The sweep slides along what it hits up to this many times.
constexpr int kSweepPasses = 3;
// A push smaller than this means the body is clear of the wall.
constexpr float kClear = 1e-4F;
// Updates the move to a climb's start point takes: 1/15 s from a run (two updates, as seen at runtime), 1/60 s
// standing (less than one update, so one; **Coney's choice** of the update counts on the fixed step).
constexpr int kRunningClimbMove = 2;
constexpr int kStandingClimbMove = 1;
// A stick longer than this gives the climb its direction; a shorter one leaves the facing.
constexpr float kClimbStickDirection = 0.01F;

raycast::Vec3 toMesh(anim::Vec3 v) { return raycast::Vec3{v.x, v.y, v.z}; }
anim::Vec3 fromMesh(raycast::Vec3 v) { return anim::Vec3{v.x, v.y, v.z}; }

// `v` across the ground, made unit length; zero when it has no horizontal part.
anim::Vec3 flatUnit(anim::Vec3 v) {
    const float length = std::hypot(v.x, v.y);
    return length > 1e-6F ? anim::Vec3{v.x / length, v.y / length, 0.0F} : anim::Vec3{};
}

// Whether a climb lifts the body onto the top at once when its second clip starts. **Coney's reading**: the page says
// the body is moved at once by the second clip's root displacement; at runtime the wall and short-wall climbs did jump
// (1.06 m forward and 1.29 m up onto a trash can, as clip 456's displacement and the top), while the fence climb was
// carried through by its second clip's own root motion. So walls move at once (rising to the top found behind the
// face), and fences go by the clip.
bool movesAtOnce(ClimbKind kind) { return kind == ClimbKind::Wall || kind == ClimbKind::ShortWall; }

// Pushes a sphere of `radius`, centred `centreHeight` above the feet at `feet`, out of the walls `filter` keeps, up
// to three times; returns where the feet end, or nothing when the sphere is still in a wall after that.
std::optional<anim::Vec3> slideOut(const raycast::CollisionMesh& mesh, anim::Vec3 feet, float radius,
                                   float centreHeight, const WallFilter& filter, std::vector<std::uint16_t>& scratch) {
    for (int pass = 0; pass <= kSweepPasses; ++pass) {
        const anim::Vec3 centre = anim::add(feet, anim::Vec3{0.0F, 0.0F, centreHeight});
        const auto push = nearestWallPush(mesh, centre, radius, filter, scratch);
        if (!push || anim::length(*push) < kClear) {
            return feet;
        }
        if (pass == kSweepPasses) {
            break;
        }
        feet = anim::add(feet, *push);
    }
    return std::nullopt;
}

} // namespace

const char* traversalName(Traversal traversal) {
    switch (traversal) {
    case Traversal::None:
        return "none";
    case Traversal::Falling:
        return "falling";
    case Traversal::Jumping:
        return "jumping";
    case Traversal::Landing:
        return "landing";
    case Traversal::RunStop:
        return "run stop";
    case Traversal::Climbing:
        return "climbing";
    }
    return "?";
}

Human::Human(const characters::AnimSet& anims, const AnimSlots& slots,
             std::span<const anim::Quat, anim::kPoseBones> bindRotations, float scale)
    : m_animator(anims, slots), m_scale(scale) {
    std::ranges::copy(bindRotations, m_bindRotations.begin());
}

float Human::speed() const { return std::hypot(m_velocity.x, m_velocity.y); }

Gait Human::gait() const { return gaitOfSpeed(anim::length(m_velocity), m_animator.speeds()); }

Traversal Human::traversal() const {
    if (m_climbRun) {
        return Traversal::Climbing;
    }
    if (m_airborne) {
        return m_jumping ? Traversal::Jumping : Traversal::Falling;
    }
    if (m_animator.actionPlaying()) {
        return m_animator.state() == AnimState::RunStop ? Traversal::RunStop : Traversal::Landing;
    }
    return Traversal::None;
}

void Human::spawn(const raycast::CollisionMesh* mesh, anim::Vec3 position, float headingDegrees) {
    m_heading = wrapAngle(headingDegrees * kPi / 180.0F);
    m_velocity = anim::Vec3{};
    m_airborne = false;
    m_outOfWorld = false;
    m_airborneUpdates = 0;
    m_blockedUpdates = 0;
    m_turn = TurnState{};
    m_stamina = Stamina(staminaTuning().maximum);
    m_sprinting = false;
    m_jumping = false;
    m_lean = 0.0F;
    endClimb();
    if (m_animator.state() != AnimState::Idle) {
        m_animator.stopToIdle();
    }
    // Creation's snap: a ray from 1 m above, 2.5 m down; on a hit the feet go 0.01 above it.
    if (mesh != nullptr) {
        const raycast::Ray ray{.origin = toMesh(anim::add(position, anim::Vec3{0.0F, 0.0F, kSnapAbove})),
                               .direction = raycast::kDown,
                               .length = kSpawnLength};
        if (const auto hit = mesh->rayCast(ray, {}, 0); hit) {
            position.z = position.z + kSnapAbove - hit->t + kSpawnGap;
            m_groundNormal = fromMesh(hit->normal);
        }
    }
    m_position = position;
    m_lastGround = position;
}

void Human::locomote() {
    const Speeds& speeds = m_animator.speeds();
    const float current = speed();
    const float target = targetSpeed(m_intent.magnitude, speeds, m_sprinting && m_stamina.value() != 0);
    const float wanted = m_intent.angle - kPi / 2.0F; // the stick's heading
    const Gait gaitNow = gaitOfSpeed(current, speeds);
    float newSpeed = approachSpeed(current, target, kStepSeconds);

    // A run stopped hard or turned back skids: the velocity is zeroed. From a sprint the run stop plays.
    const anim::Vec3 moving =
        current > 1e-6F ? anim::scale(anim::Vec3{m_velocity.x, m_velocity.y, 0.0F}, 1.0F / current) : facing(m_heading);
    if (skids(gaitNow, current, speeds, m_lastMagnitude, m_intent.magnitude, moving, facing(wanted))) {
        newSpeed = 0.0F;
        if (gaitNow == Gait::Sprint) {
            m_animator.startRunStop();
        }
    } else if (target > 0.0F) {
        // Turn toward the stick, limited by the gait and eased.
        m_heading = turnToward(m_heading, wanted, maxTurn(gaitNow), m_turn);
    }
    // While a clip moves the body (a start, a landing, a run stop) the clip alone moves it; otherwise the velocity
    // follows the facing.
    const anim::Vec3 direction = facing(m_heading);
    const float horizontal = m_animator.drivingClipPlaying() ? 0.0F : newSpeed;
    m_velocity = anim::Vec3{direction.x * horizontal, direction.y * horizontal, m_velocity.z};
}

void Human::airControl() {
    // With the stick centred nothing changes; otherwise the heading turns toward it and the velocity turns with it,
    // keeping its horizontal speed.
    if (m_intent.magnitude <= locomotionTuning().stickDeadZone) {
        return;
    }
    m_heading = turnToward(m_heading, m_intent.angle - kPi / 2.0F, airTurnLimit(), m_turn);
    const float horizontal = speed();
    const anim::Vec3 direction = facing(m_heading);
    m_velocity.x = direction.x * horizontal;
    m_velocity.y = direction.y * horizontal;
}

void Human::applyRootMotion(const anim::Pose& pose) {
    const anim::RootMotion root = anim::rootMotionOf(pose);
    // The clip's velocity is in the character's axes (facing +y): turn it by the heading, cap it, add it.
    const float c = std::cos(m_heading);
    const float s = std::sin(m_heading);
    anim::Vec3 world{root.velocity.x * c - root.velocity.y * s, root.velocity.x * s + root.velocity.y * c,
                     root.velocity.z};
    if (const float length = anim::length(world); length > kMaxSpeed) {
        world = anim::scale(world, kMaxSpeed / length);
    }
    m_velocity.x += world.x;
    m_velocity.y += world.y;
    // The turn is per 1/30 s.
    m_heading = wrapAngle(m_heading + root.turn * 30.0F * kStepSeconds);
}

std::span<const std::uint8_t> Human::passThrough() const {
    if (m_climbRun && m_climbRun->over) {
        return kFenceMaterials;
    }
    return {};
}

std::optional<anim::Vec3> Human::sweep(const raycast::CollisionMesh& mesh, anim::Vec3 from, anim::Vec3 displacement) {
    // A body that does not move meets nothing.
    if (anim::length(displacement) <= 0.0F) {
        return from;
    }
    // The walking sphere, 0.05 m clear of the feet, against the walls the move goes into and that are 0.25 m tall or
    // more; each push slides it along that wall.
    const WallFilter filter{.move = displacement, .skipLow = true, .excludeMaterials = passThrough()};
    return slideOut(mesh, anim::add(from, displacement), walkingRadius(m_scale), walkingCentreHeight(m_scale), filter,
                    m_nearby);
}

std::optional<anim::Vec3> Human::pushOutInAir(const raycast::CollisionMesh& mesh, anim::Vec3 feet) {
    // A player's push-out sphere is 0.5 × scale, centred its radius plus 0.05 above the feet, out of every wall.
    const float radius = bodyTuning().airRadius * m_scale;
    const WallFilter filter{.move = anim::Vec3{}, .skipLow = false, .excludeMaterials = passThrough()};
    return slideOut(mesh, feet, radius, radius + bodyTuning().footGap, filter, m_nearby);
}

void Human::snapToGround(const raycast::CollisionMesh& mesh, anim::Vec3 feet) {
    // While climbing over, the fence under the body is not ground (**Coney's choice**, so a low fence's top does not
    // catch the feet as they pass through it).
    const raycast::Ray ray{.origin = toMesh(anim::add(feet, anim::Vec3{0.0F, 0.0F, kSnapAbove})),
                           .direction = raycast::kDown,
                           .length = kSnapLength};
    if (const auto hit = mesh.rayCast(ray, passThrough(), 0); hit) {
        // The feet go exactly onto the hit.
        feet.z = feet.z + kSnapAbove - hit->t;
        m_position = feet;
        m_lastGround = feet;
        m_groundNormal = fromMesh(hit->normal);
        return;
    }
    // Nothing within 0.5 m below the feet: the human starts to fall.
    m_position = feet;
    m_airborne = true;
    m_airborneUpdates = 0;
}

void Human::land(anim::Vec3 feet) {
    m_lastLandingSpeed = m_velocity.z;
    m_position = feet;
    m_lastGround = feet;
    m_airborne = false;
    m_airborneUpdates = 0;
    m_velocity.z = 0.0F;
    // A jump's landing: the clip that moves on (the stick pushed) or stops.
    if (m_jumping) {
        m_jumping = false;
        m_animator.startLanding(m_intent.magnitude > locomotionTuning().stickDeadZone);
    }
}

void Human::moveOnGround(const raycast::CollisionMesh& mesh) {
    // On the ground the velocity has no z: walking climbs only through the snap. A slope slows the human, uphill and
    // downhill alike.
    m_velocity.z = 0.0F;
    const float factor = slopeFactor(m_groundNormal.z);
    const anim::Vec3 displacement{m_velocity.x * factor * kStepSeconds, m_velocity.y * factor * kStepSeconds, 0.0F};
    anim::Vec3 feet = m_position;
    if (const auto moved = sweep(mesh, m_position, displacement); moved) {
        feet = *moved;
        m_blockedUpdates = 0;
    } else {
        // Blocked: the body stays and its horizontal velocity goes.
        m_velocity.x = 0.0F;
        m_velocity.y = 0.0F;
        ++m_blockedUpdates;
    }
    snapToGround(mesh, feet);
}

void Human::moveInAir(const raycast::CollisionMesh& mesh) {
    const anim::Vec3 displacement = anim::scale(m_velocity, kStepSeconds);
    anim::Vec3 feet = anim::add(m_position, displacement);
    // Walls still push the airborne body: move across, push out, then fall straight.
    const anim::Vec3 across{m_position.x + displacement.x, m_position.y + displacement.y, m_position.z};
    if (const auto moved = pushOutInAir(mesh, across); moved) {
        feet = anim::Vec3{moved->x, moved->y, m_position.z + displacement.z};
        m_blockedUpdates = 0;
    } else {
        feet = anim::Vec3{m_position.x, m_position.y, m_position.z + displacement.z};
        ++m_blockedUpdates;
    }
    // The landing test: the segment from the body's upper point to the moved feet; a floor on it is a landing.
    const anim::Vec3 top = anim::add(m_position, anim::Vec3{0.0F, 0.0F, kLandingTestHeight});
    const anim::Vec3 segment = anim::subtract(feet, top);
    const float length = anim::length(segment);
    if (length > 1e-6F) {
        const raycast::Ray ray{
            .origin = toMesh(top), .direction = toMesh(anim::scale(segment, 1.0F / length)), .length = length};
        if (const auto hit = mesh.rayCast(ray, passThrough(), 0); hit && hit->normal.z > kFloorNormalZ) {
            m_groundNormal = fromMesh(hit->normal);
            land(anim::add(top, anim::scale(segment, hit->t / length)));
            return;
        }
    }
    m_position = feet;
}

void Human::updateMeters(bool sprintHeld) {
    // The drain at the sprint gait (nothing while an action's clip plays), which ends the sprint when it empties; the
    // refill otherwise; then the sprint flag, cleared and set again while L2 is held and stamina lasts.
    const Gait gaitNow = gait();
    const bool busy = m_animator.drivingClipPlaying() || m_climbRun.has_value();
    if (m_stamina.drain(gaitNow, busy, kStepSeconds)) {
        m_sprinting = false;
    }
    m_stamina.refill(RefillBlocks{.gait = gaitNow, .airborne = m_airborne, .sprintHeld = sprintHeld}, kStepSeconds);
    m_sprinting = sprintAsked(sprintHeld, m_stamina.value());
}

bool Human::tryContextAction() {
    // Doors, pick-ups and the like (0x002811f0) are not researched: nothing happens and the jump is tried next.
    return false;
}

bool Human::tryClimb(const raycast::CollisionMesh& mesh, anim::Vec3 direction) {
    // The forward rays reach further for a player at the run or sprint gait, who then climbs in the running form.
    const ClimbTuning& tuning = climbTuning();
    const Gait gaitNow = gait();
    const bool running = gaitNow == Gait::Run || gaitNow == Gait::Sprint;
    const auto probe = probeClimb(mesh, m_position, direction, running ? tuning.runningReach : tuning.reach, true);
    if (!probe) {
        return false;
    }
    // The face must be within the clips' reach.
    const std::uint32_t standingId = climbFirstClip(probe->kind, false);
    const std::uint32_t runningId = climbFirstClip(probe->kind, true);
    const float standingReach = clipReach(*m_animator.anims().clip(standingId));
    const float runningReach = clipReach(*m_animator.anims().clip(runningId));
    if (!withinClimbReach(probe->distance, standingReach, runningReach, running)) {
        return false;
    }
    // Face the wall and go to the start point, 0.9 of the clip's reach in front of the face.
    const anim::Vec3 out = flatUnit(probe->normal);
    m_heading = headingOf(anim::scale(out, -1.0F));
    m_turn = TurnState{};
    const float reach = running ? runningReach : standingReach;
    const anim::Vec3 start = anim::add(probe->face, anim::scale(out, tuning.startShare * reach));
    m_climbRun = ClimbRun{.firstId = running ? runningId : standingId,
                          .running = running,
                          .phase = 0,
                          .moveUpdates = running ? kRunningClimbMove : kStandingClimbMove,
                          .start = start,
                          .over = false};
    m_climbProbe = probe;
    m_velocity = anim::Vec3{};
    m_animator.startClimb(m_climbRun->firstId, running);
    return true;
}

bool Human::tryJump(const raycast::CollisionMesh* mesh, anim::Vec3 direction) {
    const float current = speed();
    const bool nearClimbable =
        mesh != nullptr && climbableAhead(*mesh, m_position, direction, jumpTuning().climbableCheck, true);
    if (!jumpAllowed(current, m_animator.speeds(), nearClimbable)) {
        return false;
    }
    // The launch: the way the human moves, at the run or sprint speed for the gait reached, and 5.5 m/s up.
    const Gait takeOff = gaitForSpeed(current, m_animator.speeds());
    const anim::Vec3 way = flatUnit(m_velocity);
    const float forward = launchSpeed(takeOff, m_animator.speeds());
    m_velocity = anim::Vec3{way.x * forward, way.y * forward, jumpTuning().upSpeed};
    m_jumping = true;
    m_airborne = true;
    m_airborneUpdates = 0;
    m_animator.startJump();
    return true;
}

void Human::tryActions(const raycast::CollisionMesh* mesh, bool sprintHeld) {
    // Nothing in the air, while climbing, or while an action's clip plays (the state and record flags).
    if (m_airborne || m_climbRun || m_animator.actionPlaying()) {
        return;
    }
    const LocomotionTuning& tuning = locomotionTuning();
    const anim::Vec3 stickWay = facing(m_intent.angle - kPi / 2.0F);
    // 1. A climb, with the stick pushed.
    if (mesh != nullptr && m_intent.magnitude > tuning.stickDeadZone) {
        const anim::Vec3 way = m_intent.magnitude > kClimbStickDirection ? stickWay : facing(m_heading);
        if (tryClimb(*mesh, way)) {
            return;
        }
    }
    // 2. The context action, unless L2 is held.
    if (!sprintHeld && tryContextAction()) {
        return;
    }
    // 3. A jump, with the stick at a run and no start clip playing. (4, the object action, is not researched.)
    if (!m_animator.startClipPlaying() && m_intent.magnitude > tuning.runThreshold) {
        static_cast<void>(tryJump(mesh, stickWay));
    }
}

void Human::endClimb() {
    m_climbRun.reset();
    m_climbProbe.reset();
}

void Human::followClimb(const raycast::CollisionMesh* mesh) {
    if (!m_climbRun) {
        return;
    }
    // The chain is over (or was replaced): the climb ends.
    const std::uint32_t id = m_animator.animId();
    ClimbRun& run = *m_climbRun;
    if (m_animator.state() != AnimState::Climb || id < run.firstId || id > run.firstId + 2) {
        endClimb();
        return;
    }
    while (run.phase < id - run.firstId) {
        ++run.phase;
        if (run.phase == 2) {
            // The last clip: fences block the body again.
            run.over = false;
            continue;
        }
        // The first clip's end: the forward probe again from 0.69 m; on failure the climb ends in the idle.
        const float length = run.running ? climbTuning().runningReprobe : climbTuning().standingReprobe;
        if (mesh == nullptr || !faceAhead(*mesh, m_position, facing(m_heading), length)) {
            endClimb();
            m_animator.stopToIdle();
            return;
        }
        run.over = true;
        if (m_climbProbe && movesAtOnce(m_climbProbe->kind)) {
            // Onto the top at once: the second clip's displacement turned to the facing, and up to the top.
            const anim::Vec3 d = m_animator.anims().clip(id)->displacement;
            const float c = std::cos(m_heading);
            const float s = std::sin(m_heading);
            m_position = anim::add(m_position, anim::Vec3{d.x * c - d.y * s, d.x * s + d.y * c, m_climbProbe->top});
            m_velocity = anim::Vec3{};
        }
    }
}

void Human::step(const HumanInput& input, const raycast::CollisionMesh* mesh) {
    // 1. The stick, turned by the camera.
    m_lastMagnitude = m_intent.magnitude;
    m_intent = stickIntent(input.stickX, input.stickY, input.cameraForward);
    if (m_outOfWorld) {
        return;
    }
    // 2. The animation's step (a climb follows its clips), and the root motion its pose carries. While a climb moves
    // the body to its start point its first clip waits: at runtime the running fence climb's first clip played its
    // whole length after that move (**Coney's reading**).
    if (!m_climbRun || m_climbRun->moveUpdates == 0) {
        m_animator.advance(kStepSeconds);
    }
    followClimb(mesh);
    const anim::Pose pose = m_animator.pose(m_bindRotations);
    // 3. The state function sets the velocity: a climb's move to its start point or its clips' root motion; a jump's
    // air control (a drop keeps its velocity); or the locomotion, with the clip's root motion added.
    const float headingBefore = m_heading;
    if (m_climbRun && m_climbRun->moveUpdates > 0) {
        const anim::Vec3 left = anim::subtract(m_climbRun->start, m_position);
        const auto updates = static_cast<float>(m_climbRun->moveUpdates);
        m_velocity = anim::Vec3{left.x / (updates * kStepSeconds), left.y / (updates * kStepSeconds), 0.0F};
        --m_climbRun->moveUpdates;
    } else if (m_climbRun) {
        m_velocity = anim::Vec3{0.0F, 0.0F, m_velocity.z};
        applyRootMotion(pose);
    } else if (m_airborne) {
        if (m_jumping) {
            airControl();
        }
    } else {
        locomote();
    }
    const float turn = wrapAngle(m_heading - headingBefore);
    // Clips move the body on the ground only (**Coney's choice**: in the air the jump keeps its launch velocity and a
    // fall the velocity it had, as at runtime).
    if (!m_airborne && !m_climbRun) {
        applyRootMotion(pose);
    }
    // 4. A velocity this long is a bug: drop it.
    if (anim::length(m_velocity) > kMaxSpeed) {
        m_velocity = anim::Vec3{};
    }
    // 5. Gravity while airborne, from the second airborne update, capped.
    if (m_airborne) {
        ++m_airborneUpdates;
        if (m_airborneUpdates >= 2) {
            m_velocity.z = std::max(m_velocity.z - kGravity * kStepSeconds, -kMaxFallSpeed);
        }
    } else {
        m_airborneUpdates = 0;
    }
    if (mesh != nullptr) {
        // 6. Out of the world: far below the lowest ground, the human stops (a mission failure in the original).
        if (m_position.z < mesh->lowestZ() - kOutOfWorldDepth) {
            m_outOfWorld = true;
            m_velocity = anim::Vec3{};
            return;
        }
        // 7. Stuck in the air for two seconds: back to the last ground.
        if (m_airborne && m_airborneUpdates > kStuckUpdates && m_blockedUpdates >= kStuckUpdates) {
            land(m_lastGround);
        } else if (m_airborne) {
            moveInAir(*mesh);
        } else {
            moveOnGround(*mesh);
        }
    } else {
        m_position = anim::add(m_position, anim::scale(m_velocity, kStepSeconds));
    }
    // A climb whose ground went away is over: the human falls.
    if (m_climbRun && m_airborne) {
        endClimb();
        m_animator.stopToIdle();
    }
    // 8. The player's part: stamina and the sprint, triangle, the lean, and the animation state.
    updateMeters(input.sprintHeld);
    if (input.actionPressed) {
        tryActions(mesh, input.sprintHeld);
    }
    m_lean = leanStep(m_lean, turn, speed(), gait());
    m_animator.choose(AnimInputs{.speed = speed(),
                                 .wantsMove = targetSpeed(m_intent.magnitude, speeds()) > 0.0F,
                                 .wantsRun = m_intent.magnitude > locomotionTuning().runThreshold,
                                 .airborne = m_airborne});
}

} // namespace coney::human
