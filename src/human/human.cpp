// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>

#include "animation/anim_task.h"
#include "combat/being_hit.h"
#include "combat/combat_tuning.h"
#include "combat/lock_on.h"
#include "core/ps2_float.h"
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
// The kept stick's magnitude falls by this each update once the stick is let go (`0x002411cc` onward).
constexpr float kLastStickFadePerStep = 0.8F;

// A human's own copy of `ranges` with its class's damage written over it, or null when it has no class table (then it
// uses `ranges` as it is). Each human keeps its own list, so its class decides only its own damage.
std::unique_ptr<combat::AnimRangeList> classRanges(const combat::AnimRangeList* ranges,
                                                   std::span<const std::int16_t> classDamage, int damagePercent) {
    if (ranges == nullptr || classDamage.empty()) {
        return nullptr;
    }
    auto own = std::make_unique<combat::AnimRangeList>(*ranges);
    combat::applyClassDamage(*own, classDamage, damagePercent);
    return own;
}

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

PlayerRecord recordOf(const HumanInput& input) {
    return PlayerRecord{.stickX = input.stickX,
                        .stickY = input.stickY,
                        .cameraForward = input.cameraForward,
                        .sprintHeld = input.sprintHeld,
                        .actionPressed = input.actionPressed,
                        .command = input.command,
                        .buttons = input.buttons,
                        .move = std::nullopt};
}

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
             std::span<const anim::Quat, anim::kPoseBones> bindRotations, float scale,
             const combat::AnimRangeList* ranges, std::span<const std::int16_t> classDamage, int damagePercent)
    : m_animator(anims, slots), m_ownRanges(classRanges(ranges, classDamage, damagePercent)),
      m_ranges(m_ownRanges != nullptr ? m_ownRanges.get() : ranges), m_fighter(m_ranges), m_scale(scale) {
    std::ranges::copy(bindRotations, m_bindRotations.begin());
}

void Human::setFighterProfile(const FighterProfile& profile) {
    m_profile = profile;
    m_fighter = Fighter(m_ranges, 1, m_profile);
}

TargetState Human::state() const {
    const Victim& victim = m_fighter.victim();
    if (m_fighter.health().depleted() || victim.grounded()) {
        return TargetState::Grounded;
    }
    return m_fighter.grabbed() ? TargetState::Held : TargetState::Standing;
}

float Human::speed() const { return std::hypot(m_velocity.x, m_velocity.y); }

Gait Human::gait() const { return gaitOfSpeed(anim::length(m_velocity), m_animator.speeds()); }

GateInput Human::gateInput() const {
    // **Coney choice**: no state code is kept. The original's 5 is the locomotion's own mark for turning in place,
    // written while the velocity is already gated (the idle's fade, a skid), and 6 is not traced; neither gates an
    // update the record's bits do not.
    return GateInput{
        .flags = m_animator.flags(), .stateCode = 0, .airborne = m_airborne, .attached = m_fighter.grabbed()};
}

bool Human::stickHeld() const {
    const GateInput gate = gateInput();
    return m_fighter.holdsMovement(m_animator) || stickBusy(gate) || stickVelocityGated(gate);
}

Traversal Human::traversal() const {
    if (m_climbRun) {
        return Traversal::Climbing;
    }
    if (m_airborne) {
        return m_jumping ? Traversal::Jumping : Traversal::Falling;
    }
    if (m_animator.actionPlaying() && m_animator.state() != AnimState::Attack) {
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
    m_landingPending = false;
    m_turn = TurnState{};
    m_lastStick = StickIntent{};
    m_stamina = Stamina(staminaTuning().maximum);
    m_sprinting = false;
    m_jumping = false;
    m_lean = 0.0F;
    m_fighter = Fighter(m_ranges, 1, m_profile);
    m_announced.clear();
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

void Human::locomote(bool gated) {
    const Speeds& speeds = m_animator.speeds();
    // The current speed is the velocity's length as the original measures it (vtable `+0x94`), in single precision
    // rounded toward zero as the PS2's unit rounds: at a steady run it comes out a few units in the last place either
    // side of the run speed the velocity was set to, mostly below, which decides whether a release skids.
    // **Coney's choice**: the velocity itself (facing × speed) is IEEE's; with the length rounded the PS2's way a
    // steady run reads at or above the run speed in about 6 % of headings, the original's ~1 release in 10.
    const float current = ps2::length(m_velocity.x, m_velocity.y);
    const float target = m_moveSpeed.has_value()
                             ? *m_moveSpeed
                             : targetSpeed(m_intent.magnitude, speeds, m_sprinting && m_stamina.value() != 0);
    const float wanted = m_intent.angle - kPi / 2.0F; // the stick's heading
    const Gait gaitNow = gaitOfSpeed(current, speeds);
    float newSpeed = approachSpeed(current, target, kStepSeconds);

    // A run stopped hard or turned back at or above the run speed skids: the velocity is zeroed and the run stop plays
    // (always after a sprint, rarely after a steady run, docs/research/characters.md#run-stop). The run stop's 0x80000
    // then makes the human busy, so it holds the facing; the skid's own update still turns one step (a reversal at a
    // run turned 18° before sliding the old way). The skid sets no state code: the run stop's own bits gate its
    // updates.
    const anim::Vec3 moving =
        current > 1e-6F ? anim::scale(anim::Vec3{m_velocity.x, m_velocity.y, 0.0F}, 1.0F / current) : facing(m_heading);
    if (skids(gaitNow, current, speeds, m_lastMagnitude, m_intent.magnitude, moving, facing(wanted))) {
        newSpeed = 0.0F;
        m_animator.startRunStop();
    }
    // A brain's move at speed 0 still turns the human to its heading: the turn on the spot
    // (docs/research/ai.md#moving).
    if (target > 0.0F || m_moveSpeed.has_value()) {
        // Turn toward the stick, limited by the gait and eased.
        m_heading = turnToward(m_heading, wanted, maxTurn(gaitNow), m_turn);
    } else if (m_animator.idleFading() && m_lastStick.magnitude > locomotionTuning().stickDeadZone) {
        // Let go, while the idle's fade holds 0x10000000 the body keeps turning toward the stick's last angle, its
        // magnitude falling by 0.8 an update (`+0x5d8` / `+0x5dc`, docs/research/characters.md#run-stop): the 0.7°,
        // 1.1°, 1.4°, 1.5° after run_circle's release, eased from a fresh start at the standing limit.
        m_heading = turnToward(m_heading, m_lastStick.angle - kPi / 2.0F, maxTurn(gaitNow), m_turn);
    } else {
        // No turn this update: the next one starts without the last step's carry (at runtime the first turn after a
        // release carried nothing of the run's).
        m_turn = TurnState{};
    }
    // The locomotion gate: while a clip moves the body (a start, a landing, a recovery: the record's 0x110c0880) or
    // the state code is 5 or 6, the clip alone moves it. Standing (no gait blend yet), the update the start clip
    // begins does not move the body either: at runtime the first update with the stick pushed began the start clip at
    // speed 0. Otherwise the velocity follows the facing.
    const anim::Vec3 direction = facing(m_heading);
    const bool clipMoves = gated || !m_animator.gaitBlendPlaying();
    const float horizontal = clipMoves ? 0.0F : newSpeed;
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
    // The clip's velocity is in the character's axes (facing +y): turn it by the heading, scale it by the body (the
    // walk start moved Rembrandt at 0.762 m/s against the clip's 0.786, docs/research/characters.md#locomotion), cap
    // it, add it.
    const float c = std::cos(m_heading);
    const float s = std::sin(m_heading);
    anim::Vec3 world{(root.velocity.x * c - root.velocity.y * s) * m_scale,
                     (root.velocity.x * s + root.velocity.y * c) * m_scale, root.velocity.z * m_scale};
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
    // The player's walking sphere (0.485 m for Rembrandt), 0.05 m clear of the feet, against the walls the move goes
    // into and that are 0.25 m tall or more; each push slides it along that wall.
    const WallFilter filter{
        .move = displacement, .skipLow = true, .excludeMaterials = passThrough(), .toContact = true};
    return slideOut(mesh, anim::add(from, displacement), playerWalkingRadius(m_scale),
                    playerWalkingCentreHeight(m_scale), filter, m_nearby);
}

std::optional<anim::Vec3> Human::pushOutInAir(const raycast::CollisionMesh& mesh, anim::Vec3 feet) {
    // A player's push-out sphere is 0.5 × scale, centred its radius plus 0.05 above the feet, out of every wall.
    const float radius = bodyTuning().airRadius * m_scale;
    const WallFilter filter{
        .move = anim::Vec3{}, .skipLow = false, .excludeMaterials = passThrough(), .toContact = false};
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
    m_landingPending = false;
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
        keepSlidVelocity(anim::subtract(*moved, m_position), displacement, factor);
    } else {
        // Blocked: the body stays and its horizontal velocity goes.
        m_velocity.x = 0.0F;
        m_velocity.y = 0.0F;
        ++m_blockedUpdates;
    }
    snapToGround(mesh, feet);
}

void Human::keepSlidVelocity(anim::Vec3 slid, anim::Vec3 displacement, float factor) {
    // Only a move the walls changed: the velocity becomes the move the sweep allowed, never longer than it was, so the
    // next update's speed starts from what the wall left of it.
    const anim::Vec3 change{slid.x - displacement.x, slid.y - displacement.y, 0.0F};
    if (anim::length(change) < kClear || factor <= 0.0F) {
        return;
    }
    const float before = std::hypot(m_velocity.x, m_velocity.y);
    anim::Vec3 velocity{slid.x / (factor * kStepSeconds), slid.y / (factor * kStepSeconds), 0.0F};
    if (const float after = anim::length(velocity); after > before && after > 0.0F) {
        velocity = anim::scale(velocity, before / after);
    }
    m_velocity.x = velocity.x;
    m_velocity.y = velocity.y;
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
    // A floor an earlier move passed, now kLandingDepth or more above the feet: land on it, the whole horizontal move
    // made (at runtime the landing update still moved at the run's 7.80 m/s, docs/research/characters.md#falling).
    // Shallower, the body falls a whole step more through it. Nothing under the feet any more (they went over an
    // edge): no landing, the fall goes on.
    if (m_landingPending && m_landingFloorZ - m_position.z >= kLandingDepth) {
        const raycast::Ray ray{.origin = toMesh(anim::Vec3{feet.x, feet.y, m_landingFloorZ + kSnapAbove}),
                               .direction = raycast::kDown,
                               .length = kSnapLength};
        if (const auto hit = mesh.rayCast(ray, passThrough(), 0); hit && hit->normal.z > kFloorNormalZ) {
            m_groundNormal = fromMesh(hit->normal);
            m_velocity.z = m_landingSpeed;
            land(anim::Vec3{feet.x, feet.y, m_landingFloorZ + kSnapAbove - hit->t});
            return;
        }
    }
    // The landing test: the segment from the body's upper point to the moved feet; a floor on it is remembered, and
    // landed on by the first update that starts kLandingDepth or more below it.
    m_landingPending = false;
    const anim::Vec3 top = anim::add(m_position, anim::Vec3{0.0F, 0.0F, kLandingTestHeight});
    const anim::Vec3 segment = anim::subtract(feet, top);
    const float length = anim::length(segment);
    if (length > 1e-6F) {
        const raycast::Ray ray{
            .origin = toMesh(top), .direction = toMesh(anim::scale(segment, 1.0F / length)), .length = length};
        if (const auto hit = mesh.rayCast(ray, passThrough(), 0); hit && hit->normal.z > kFloorNormalZ) {
            m_landingPending = true;
            m_landingFloorZ = top.z + segment.z * (hit->t / length);
            m_landingSpeed = m_velocity.z;
        }
    }
    m_position = feet;
}

void Human::holdForCombat() {
    // The clip moves the body (its root motion is added after this), with a grab's alignment slide.
    const anim::Vec3 slide = m_fighter.takeSlide();
    m_velocity = anim::Vec3{slide.x, slide.y, m_velocity.z};
    // Blocking, the stick turns the player in place at the combat stance's limit (24° an update in play; the block is
    // held in a fight stance, docs/research/combat.md).
    if (m_fighter.blocking() && m_intent.magnitude > locomotionTuning().stickDeadZone) {
        m_heading = turnToward(m_heading, m_intent.angle - kPi / 2.0F, stanceTurn(), m_turn);
    }
    // A standing grab: the stick near full turns the grabber's back to it and walks the pair backward.
    const anim::Vec3 pull = m_fighter.moveGrab(m_intent.angle - kPi / 2.0F, m_intent.magnitude, m_animator, m_heading);
    if (anim::length(pull) > 0.0F) {
        m_velocity = anim::Vec3{pull.x, pull.y, m_velocity.z};
    }
}

void Human::combatWalk(const Combatant& target, bool gated) {
    // Faces the target every update.
    const anim::Vec3 to = anim::subtract(target.position(), m_position);
    if (std::hypot(to.x, to.y) > 1e-4F) {
        m_heading = headingOf(to);
    }
    m_turn = TurnState{};
    // The gate (a recovery, the idle after a block): no velocity, and the clip playing goes on.
    if (gated) {
        m_velocity = anim::Vec3{0.0F, 0.0F, m_velocity.z};
        return;
    }
    // The stick, normalised, walks the human at one speed whatever its deflection; at rest it stands in the fight idle.
    if (m_intent.magnitude <= locomotionTuning().stickDeadZone) {
        m_velocity = anim::Vec3{0.0F, 0.0F, m_velocity.z};
        m_animator.playCombatWalk(kAnimFightIdle);
        return;
    }
    const float speed = combat::combatTuning().combatWalkSpeed;
    m_velocity = anim::Vec3{std::cos(m_intent.angle) * speed, std::sin(m_intent.angle) * speed, m_velocity.z};
    // The clip by the stick's angle from the facing, clockwise (record +0xdc).
    const float stickHeading = m_intent.angle - kPi / 2.0F;
    const float clockwise = -wrapAngle(stickHeading - m_heading) * 180.0F / kPi;
    m_animator.playCombatWalk(static_cast<std::uint32_t>(combat::combatWalkClip(clockwise)));
}

void Human::fight(std::span<Combatant* const> targets) {
    // The stick in the facing frame: x to the player's right, y ahead.
    const float relative = wrapAngle(m_intent.angle - kPi / 2.0F - m_heading);
    const combat::Stick stick{-m_intent.magnitude * std::sin(relative), m_intent.magnitude * std::cos(relative)};
    // Game time from the updates stepped (whole milliseconds, as the original keeps it).
    const std::uint64_t nowMs = m_updates * 1000 / 30;
    m_fighter.update(FighterInput{.command = m_record.command,
                                  .buttons = m_record.buttons,
                                  .stick = stick,
                                  .padStick = combat::Stick{m_record.stickX, m_record.stickY},
                                  .gait = gait(),
                                  .position = m_position,
                                  .heading = m_heading,
                                  .nowMs = nowMs,
                                  .targets = targets},
                     m_animator, m_heading);
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
    const Gait takeOff = gaitOfSpeed(current, m_animator.speeds());
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
    // Nothing in the air or while climbing; nothing while the record's +0x08 drops the dispatcher's commands (a
    // recovery, a landing) or makes the human busy (an attack's phases, the run stop). **Coney choice**: triangle's
    // actions take the dispatcher's mask and the stick step's, as they take the stick's direction.
    if (m_airborne || m_climbRun || (m_animator.flags() & combat::kDispatchDroppingPhases) != 0 ||
        stickBusy(gateInput())) {
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
    // 3. A jump, with the stick at a run; a start clip does not stop it (a tap 7 updates into the run start jumped at
    // runtime, docs/research/feel.md). (4, the object action, is not researched.)
    if (m_intent.magnitude > tuning.runThreshold) {
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
        run.clipChanged = true;
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
    m_record = recordOf(input);
    animate(mesh);
    updateState(mesh);
    updateActions(input.targets, mesh);
}

void Human::animate(const raycast::CollisionMesh* mesh) {
    if (m_outOfWorld) {
        return;
    }
    // The animation's step (a climb follows its clips). A climb's first clip runs during the move to the start point,
    // as the original installs it with that move (0x0023d2b8, docs/research/characters.md#climb). The clip's warning
    // events reach the target as they pass.
    const anim::AnimTask* before = m_animator.tasks().top();
    const std::uint32_t beforeId = before != nullptr ? before->animId() : 0;
    const float beforeTime = before != nullptr ? before->time() : 0.0F;
    m_animator.advance(kStepSeconds);
    followClimb(mesh);
    sendWarnings(before, beforeId, beforeTime);
}

void Human::sendWarnings(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime) {
    Combatant* target = m_fighter.target();
    const anim::AnimTask* top = m_animator.tasks().top();
    if (target == nullptr || top == nullptr || top->eventClip() == nullptr) {
        return;
    }
    // From the clip's start when it began this step (or another clip took the top), else from where it was.
    const bool same = top == before && top->animId() == beforeId && top->time() >= beforeTime;
    const auto warning = combat::warningBetween(*top->eventClip(), same ? beforeTime : -1.0F, top->time());
    if (!warning.has_value()) {
        return;
    }
    // Only a target within twice the reach of the anim playing (its Anim Range List +0x04) hears it.
    const combat::AnimRange* range = m_ranges != nullptr ? m_ranges->find(top->animId()) : nullptr;
    const float reach = range != nullptr ? range->reach : 0.0F;
    const anim::Vec3 to = anim::subtract(target->position(), m_position);
    if (std::hypot(to.x, to.y) > combat::kWarningReachScale * reach) {
        return;
    }
    target->warn(AttackNotice{.warning = *warning,
                              .attackAnim = static_cast<int>(top->animId()),
                              .code = range != nullptr ? range->kind : 0,
                              .attacker = m_position});
}

void Human::updateState(const raycast::CollisionMesh* mesh) {
    // 1. The record's stick, turned by its camera.
    ++m_updates;
    m_lastMagnitude = m_intent.magnitude;
    m_intent = stickIntent(m_record.stickX, m_record.stickY, m_record.cameraForward);
    // The stick's last angle and magnitude, kept while it is pushed and fading by 0.8 an update once it is let go.
    if (m_intent.magnitude > locomotionTuning().stickDeadZone) {
        m_lastStick = m_intent;
    } else {
        m_lastStick.magnitude *= kLastStickFadePerStep;
    }
    // A brain's move replaces the stick: its heading as the stick's direction, pushed fully while it moves.
    m_moveSpeed.reset();
    if (m_record.move.has_value()) {
        m_moveSpeed = std::max(0.0F, m_record.move->speed);
        m_intent =
            StickIntent{.angle = m_record.move->heading + kPi / 2.0F, .magnitude = *m_moveSpeed > 0.0F ? 1.0F : 0.0F};
    }
    if (m_outOfWorld) {
        return;
    }
    // 2. The root motion the animation's pose carries.
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
        // The update the chain goes on to its next clip moves nothing: at runtime the first update of 441 and of 442
        // moved the body 0 m (confirmed (runtime); the reason is not traced).
        m_velocity = anim::Vec3{0.0F, 0.0F, m_velocity.z};
        if (!m_climbRun->clipChanged) {
            applyRootMotion(pose);
        }
        m_climbRun->clipChanged = false;
    } else if (m_airborne) {
        if (m_jumping) {
            airControl();
        }
    } else if (const GateInput gate = gateInput(); m_fighter.holdsMovement(m_animator) || stickBusy(gate)) {
        // The stick step is skipped: combat's states, or the record's +0x08 makes the human busy.
        holdForCombat();
    } else if (const Combatant* lock = m_fighter.lockTarget(); lock != nullptr) {
        combatWalk(*lock, stickVelocityGated(gate));
    } else {
        // Out of the lock the locomotion's clips come back.
        m_animator.leaveCombatWalk();
        locomote(stickVelocityGated(gate));
    }
    // An attack's steer onto its target (its turn and slide at a constant rate, `0x0023f5e0` from the state update)
    // on top of what the state function set; the clip's root motion is added after it.
    if (!m_airborne && !m_climbRun) {
        const TurnAndSlideStep steer = m_fighter.takeSteer(kStepSeconds);
        m_heading = wrapAngle(m_heading + steer.turn);
        m_velocity.x += steer.velocity.x;
        m_velocity.y += steer.velocity.y;
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
    m_lastTurn = turn;
}

void Human::updateActions(std::span<Combatant* const> targets, const raycast::CollisionMesh* mesh) {
    if (m_outOfWorld) {
        return;
    }
    // Stamina and the sprint (a block clears it), combat, triangle, the lean, and the animation state. Combat first,
    // as the original's dispatcher reads the block and the chain before the commands; triangle keeps its climb,
    // context action and jump while combat does not hold the body (in a grab it mugs).
    updateMeters(m_record.sprintHeld && !m_fighter.blocking());
    if (!m_airborne && !m_climbRun) {
        fight(targets);
    }
    if (m_record.actionPressed && !m_fighter.holdsMovement(m_animator)) {
        tryActions(mesh, m_record.sprintHeld);
    }
    m_lean = leanStep(m_lean, m_lastTurn, speed(), gait());
    m_animator.choose(AnimInputs{
        .speed = speed(),
        .wantsMove = m_moveSpeed.has_value() ? *m_moveSpeed > 0.0F : targetSpeed(m_intent.magnitude, speeds()) > 0.0F,
        .wantsRun = m_moveSpeed.has_value() ? *m_moveSpeed >= speeds().run
                                            : m_intent.magnitude > locomotionTuning().runThreshold,
        .airborne = m_airborne});
}

} // namespace coney::human
