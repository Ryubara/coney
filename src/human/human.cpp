// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>

#include "animation/anim_clip.h"
#include "animation/anim_task.h"
#include "combat/anim_ids.h"
#include "combat/attacks.h"
#include "combat/being_hit.h"
#include "combat/combat_tuning.h"
#include "combat/lock_on.h"
#include "core/assert.h"
#include "core/pad.h"
#include "core/ps2_float.h"
#include "human/body.h"
#include "human/fighter_clips.h"
#include "human/jump.h"
#include "human/step_control.h"
#include "physics/body_sweep.h"
#include "physics/mesh_sweep.h"
#include "physics/sphere_triangle.h"
#include "world_objects/pickups.h"

namespace coney::human {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// The triangle flag of covered ground (onCoveredGround()).
constexpr std::uint16_t kCoveredGroundFlag = 0x20;
// A human's capsule (docs/research/physics.md): its radius and height before the body scale.
constexpr float kStrikeCapsuleRadius = 0.35F;
constexpr float kStrikeCapsuleHeight = 2.0F;
// The struck capsule's top above the head bone, and its least height, before the body scale
// (`PhysicsCapsule_Pose` 0x00343518, docs/research/combat/index.md#capsule-strike).
constexpr float kCapsuleAboveHead = 0.2F;
/// The bone the led steer aims at: the head, which human +0x500 holds (docs/research/combat-targets.md#led-steer).
constexpr int kLedPointBone = 6;
// Beyond this between the feet no strike shape can meet another body's (each reaches well under 1.5 m from its feet),
// so the victim is not posed.
constexpr float kStrikeBroadReach = 3.0F;
// **Coney's reading** of a charge stopping dead at a wall (docs/research/combat/index.md#moving-strikes):
// `Human_OnContact` answers the slide response, which takes the move into the wall away, so a charge whose sweep kept
// less than this share of its move met the wall head-on; it then does not move again before its clip ends, as seen at
// runtime (why is not traced).
constexpr float kChargeStopShare = 0.5F;
// The idle a pick-up returns to (388, the plain idle).
constexpr std::uint32_t kPickUpIdle = 388;
// The blend into the uncuff's clips, the freer's 325 and the cuffed human's 326 (AnimTaskBlendOut 0.01 s in
// Uncuff_Start, 0x00260ca8).
constexpr float kUncuffFade = 0.01F;
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

// The walking body's owner as the physics world's sweep sees it (docs/research/physics-bodies.md#u3-records), for one
// sweep: the human's feet as the sweep moves them, the velocity it writes back, and `Human_OnContact`'s answer for the
// level.
class WalkingOwner final : public physics::BodyOwner {
  public:
    WalkingOwner(const raycast::CollisionMesh& mesh, anim::Vec3 start, bool airborne, bool inPairOrBlock,
                 std::span<const std::uint8_t> passThrough)
        : m_mesh(mesh), m_start(start), m_position(start), m_airborne(airborne), m_inPairOrBlock(inPairOrBlock),
          m_passThrough(passThrough) {}

    [[nodiscard]] std::uint32_t typeMask() const override { return physics::kTypeHuman; }
    [[nodiscard]] bool airborne() const override { return m_airborne; }
    // The human copies its transform before it moves, so the cached position is where the sweep starts.
    [[nodiscard]] anim::Vec3 cachedPosition() const override { return m_start; }
    [[nodiscard]] anim::Vec3 position() const override { return m_position; }
    void setPosition(anim::Vec3 position) override { m_position = position; }
    void setVelocity(anim::Vec3 velocity) override { m_written = velocity; }
    // **Coney's choice**: 1 for the weight and the push weight (the human's pushes are already divided by its push
    // weight when they are added, addPush(), and the body carries kBodyPushUndivided), 0 restitution (the level's
    // answer is a slide, never a bounce).
    [[nodiscard]] float attribute(int which) const override {
        return which == physics::kAttributeRestitution ? 0.0F : 1.0F;
    }
    [[nodiscard]] physics::Body* body() override { return m_body; }
    // `Human_OnContact` (`0x00219d50`) for the level: a climbing human passes through the fence it climbs (code 0);
    // every other triangle slides, with the contact scale (`0x20001`; docs/research/characters.md#walls, step 4).
    [[nodiscard]] std::uint32_t onContact(const physics::Contact& contact) override {
        if (contact.triangle && !m_passThrough.empty()) {
            const std::uint8_t material = m_mesh.triangles()[*contact.triangle].material;
            if (std::ranges::find(m_passThrough, material) != m_passThrough.end()) {
                return 0;
            }
        }
        return physics::kAnswerScale | physics::kCodeSlide;
    }
    [[nodiscard]] bool inPairOrBlock() const override { return m_inPairOrBlock; }

    // Gives the body this owner answers for.
    void setBody(physics::Body* body) { m_body = body; }
    // The velocity the sweep wrote back, if any.
    [[nodiscard]] std::optional<anim::Vec3> written() const { return m_written; }

  private:
    const raycast::CollisionMesh& m_mesh;
    anim::Vec3 m_start;
    anim::Vec3 m_position;
    bool m_airborne;
    bool m_inPairOrBlock;
    std::span<const std::uint8_t> m_passThrough;
    physics::Body* m_body = nullptr;
    std::optional<anim::Vec3> m_written;
};

// The capsule strike (docs/research/combat/index.md#capsule-strike): whether any bone strike shape of `shapes` (posed
// now; `before`, the update before) meets the target's upright `capsule`. A segment is tested where it stands, against
// the capsule's axis; a sphere along its path this update. The attacker's own capsule (bone -1) does not strike.
// @orig 0x00349a60 Sweep_SphereCapsule (unknown)
bool meetsCapsule(std::span<const PosedShape> before, std::span<const PosedShape> shapes, const PosedShape& capsule) {
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const PosedShape& shape = shapes[i];
        if (shape.bone < 0) {
            continue;
        }
        const bool sphere = shape.a.x == shape.b.x && shape.a.y == shape.b.y && shape.a.z == shape.b.z;
        const PosedShape tested =
            sphere ? PosedShape{.a = before[i].a, .b = shape.a, .radius = shape.radius, .bone = shape.bone} : shape;
        if (shapesOverlap(tested, capsule)) {
            return true;
        }
    }
    return false;
}

} // namespace

PlayerRecord recordOf(const HumanInput& input) {
    return PlayerRecord{.stickX = input.stickX,
                        .stickY = input.stickY,
                        .cameraForward = input.cameraForward,
                        .sprintHeld = input.sprintHeld,
                        .actionPressed = input.actionPressed,
                        .command = input.command,
                        .climbToward = std::nullopt,
                        .padCommand = combat::command::kNone,
                        .buttons = input.buttons,
                        .padDriven = true,
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
      m_classDamage(classDamage.begin(), classDamage.end()), m_damagePercent(damagePercent),
      m_ranges(m_ownRanges != nullptr ? m_ownRanges.get() : ranges), m_fighter(m_ranges), m_scale(scale) {
    std::ranges::copy(bindRotations, m_bindRotations.begin());
}

std::vector<HumanSound> Human::takeSounds() {
    std::vector<HumanSound> sounds;
    // Clip event 11 carries the animation sound's id in its argument (0 plays nothing); 0x45 stops the sound the
    // human holds (docs/research/sound-events.md#spray-loop).
    for (const anim::ClipEvent& event : m_animator.takeEvents()) {
        if (event.type == kEventAnimSound) {
            sounds.push_back(HumanSound{
                .kind = HumanSound::Kind::Anim, .animSound = event.argument, .at = {}, .sceneFeet = std::nullopt});
        } else if (event.type == kEventStopHeldSound) {
            sounds.push_back(
                HumanSound{.kind = HumanSound::Kind::StopHeld, .animSound = 0, .at = {}, .sceneFeet = std::nullopt});
        }
    }
    sounds.insert(sounds.end(), m_contactSounds.begin(), m_contactSounds.end());
    m_contactSounds.clear();
    std::vector<HumanSound> hits = m_fighter.takeSounds();
    sounds.insert(sounds.end(), hits.begin(), hits.end());
    return sounds;
}

void Human::reportSound(const HumanSound& sound) {
    if (m_reportSounds) {
        m_contactSounds.push_back(sound);
    }
}

void Human::reportSounds(bool on) {
    // Switching it on again keeps what it holds: the animator's keepEvents() drops the kept clip events.
    if (on == m_reportSounds) {
        return;
    }
    m_reportSounds = on;
    m_animator.keepEvents(on);
}

void Human::applyDamageModifier(float factor) {
    if (m_ownRanges == nullptr) {
        return;
    }
    // The class's values scaled and rounded; a 0 is not written, so that entry keeps its damage.
    std::vector<std::int16_t> scaled(m_classDamage.size());
    for (std::size_t i = 0; i < scaled.size(); ++i) {
        scaled[i] = static_cast<std::int16_t>(std::lround(static_cast<float>(m_classDamage[i]) * factor));
    }
    combat::applyClassDamage(*m_ownRanges, scaled, m_damagePercent);
}

void Human::setFighterProfile(const FighterProfile& profile) {
    m_profile = profile;
    m_fighter = Fighter(m_ranges, 1, m_profile);
    // Its stamina is its power class's, full (`Human_Init`'s defaults, docs/research/characters.md#stats).
    m_stamina =
        Stamina(m_profile.powerClass.staminaMax, static_cast<float>(m_profile.powerClass.staminaRefillPerSecond));
    m_fighter.setLogName(m_logName);
}

void Human::changeCharacter(const characters::AnimSet& anims, const combat::AnimRangeList* ranges,
                            std::span<const std::int16_t> classDamage, int damagePercent,
                            const FighterProfile& profile) {
    m_animator.changeAnims(anims);
    m_idleClipName.clear();
    m_overrideNames.clear();
    // The fighter reads the list, so the new list first; the old copy goes once the fighter no longer holds it.
    std::unique_ptr<combat::AnimRangeList> own = classRanges(ranges, classDamage, damagePercent);
    m_ranges = own != nullptr ? own.get() : ranges;
    setFighterProfile(profile);
    m_ownRanges = std::move(own);
}

TargetState Human::state() const {
    const Victim& victim = m_fighter.victim();
    if (m_fighter.health().depleted()) {
        return TargetState::Grounded;
    }
    // Held or mounted by a grabber that drives it (Fighter::enterHold()).
    if (const auto held = m_fighter.holdState(); held.has_value()) {
        return *held;
    }
    if (victim.grounded()) {
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
    return GateInput{.flags = m_animator.flags(),
                     .stateCode = 0,
                     .airborne = m_airborne,
                     .attached = m_fighter.grabbed() || m_fighter.holdAttached()};
}

bool Human::stickHeld() const {
    const GateInput gate = gateInput();
    return m_script.arrested || m_script.workingOut || m_fighter.holdsMovement(m_animator) || stickBusy(gate) ||
           stickVelocityGated(gate);
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

void Human::standAfresh(bool keepMeters) {
    m_velocity = anim::Vec3{};
    m_airborne = false;
    m_outOfWorld = false;
    m_airborneUpdates = 0;
    m_blockedUpdates = 0;
    m_landingPending = false;
    m_turn = TurnState{};
    m_lastStick = StickIntent{};
    const Stamina stamina = m_stamina;
    m_stamina =
        Stamina(m_profile.powerClass.staminaMax, static_cast<float>(m_profile.powerClass.staminaRefillPerSecond));
    m_sprinting = false;
    m_jumping = false;
    m_strikes.clear();
    m_chargeStopped = false;
    m_lean = 0.0F;
    // The fighting starts afresh (no stun, no stance, no attack; for a creation full health and meters too) with the
    // flags the scripts set kept. **Coney's choice** for a character change or a settle of a human in a pair: the
    // pair is broken first (Human_BreakPair), so its partner is let go.
    m_fighter.breakPair();
    const std::uint64_t flags = m_fighter.flags();
    const std::optional<TargetState> broken = m_fighter.takeBrokenHold();
    const combat::Health health = m_fighter.health();
    const int power = m_fighter.combat().power().value();
    const int rage = m_fighter.combat().rage().value();
    m_fighter = Fighter(m_ranges, 1, m_profile);
    if (keepMeters) {
        m_stamina = stamina;
        m_fighter.health() = health;
        m_fighter.combat().power().set(power);
        m_fighter.combat().rage().set(rage);
    }
    m_fighter.setFlags(flags);
    m_fighter.setLogName(m_logName);
    m_fighter.setBrokenHold(broken);
    m_announced.clear();
    endClimb();
    // A working-out human's clips stay: a character change of a human that is working out keeps them until the
    // workout ends (docs/research/workouts.md#start).
    if (m_animator.state() != AnimState::Idle && !m_script.workingOut) {
        m_animator.stopToIdle();
    }
}

void Human::settle() { standAfresh(true); }

void Human::spawn(const raycast::CollisionMesh* mesh, anim::Vec3 position, float headingDegrees) {
    m_heading = wrapAngle(headingDegrees * kPi / 180.0F);
    standAfresh(false);
    // Creation's snap: a ray from 1 m above, 2.5 m down; on a hit the feet go 0.01 above it. Human_Init skips it in
    // game modes 0xb and 0x11, where Coney makes no human. **Coney's choice**: Human_Init then sets the point through
    // Human_SetTransform, whose 0.5 m drop would take the 0.01 back for a human in no scene; whether +0x280 is -1 that
    // early is not traced (Human_Init sets it to -1 only later), so the gap stays.
    if (mesh != nullptr) {
        const raycast::Ray ray{.origin = toMesh(anim::add(position, anim::Vec3{0.0F, 0.0F, kSnapAbove})),
                               .direction = raycast::kDown,
                               .length = kSpawnLength};
        if (const auto hit = mesh->rayCast(ray, {}, 0); hit) {
            position.z = position.z + kSnapAbove - hit->t + kSpawnGap;
            m_groundNormal = fromMesh(hit->normal);
            m_groundMaterial = hit->material;
        }
    }
    m_position = position;
    m_lastGround = position;
}

void Human::place(const raycast::CollisionMesh* ground, anim::Vec3 position, float headingDegrees) {
    // Any pair is broken first, its partner playing its reaction on its next update (Human_BreakPair,
    // docs/research/combat-grabs.md#pair-break).
    m_fighter.breakPair();
    // The drop: a 0.5 m ray straight down from the point itself; on a hit the feet go onto it, with no gap. A human in
    // a scene (no ground given) stands exactly where put.
    if (ground != nullptr) {
        const raycast::Ray ray{.origin = toMesh(position), .direction = raycast::kDown, .length = kPlaceDrop};
        if (const auto hit = ground->rayCast(ray, {}, 0); hit) {
            position.z -= hit->t;
        }
    }
    // The transform (the position slot, Human_SetPosition). The point is the human's last ground too, so the
    // stuck-in-the-air rule lands him near it and not where he stood before.
    m_position = position;
    m_lastGround = position;
    m_heading = wrapAngle(headingDegrees * kPi / 180.0F);
    // The stick's last angle (+0x5d8) is the new facing's angle from x, its push (+0x5dc) none, so an idle's fade
    // turns the human nowhere.
    m_lastStick = StickIntent{.angle = wrapAngle(m_heading + (kPi / 2.0F)), .magnitude = 0.0F};
    // Not ported, each for want of the state it marks (docs/research/teleports.md#placement):
    // - the placed marks: +0x1b3 = 1 tells Human_GetNavPolygon to look the human's polygon up again, but Coney
    //   caches none (ai::RoutePlanner finds it at each query); object flag 0x40000000 at +0x54 with the time at +0x50
    //   has no reader traced and Coney's human has no such flags word;
    // - bit 0x80000000 on the physics body's flags (+0x1a0, +0x40, "re-pose and re-sort"): Coney's body is posed
    //   from the position at each use (human/body.h), so it is never stale;
    // - the brain's facing and front-action pop: ai::Brain::placed(), the callers' (the human knows no brain).
    // **Coney's choice**: a climb in progress ends; how the original's climb goes on after a placement is not traced
    // (docs/research/teleports.md#q-placement-rest).
    endClimb();
}

void Human::resetMotion() {
    // Human_SetMoveVelocity with the zero vector: the move velocity across the ground; the vertical speed is a field of
    // its own (+0x3a0) and stays.
    m_velocity.x = 0.0F;
    m_velocity.y = 0.0F;
    // The steer: Human_ClearTurn's fields (+0x300, +0x304, +0x331, +0x332), and the turn's rate +0x308 and the
    // slide's velocity +0x2e0-+0x2ec (set to (0, 0, 0, 1)), which Human_ResetMotion clears beside them
    // (docs/research/combat-targets.md#fight-stance); Fighter::clearSteer() drops the whole turn and slide.
    m_fighter.clearSteer();
}

void Human::putBack(const raycast::CollisionMesh* ground, anim::Vec3 position, float headingDegrees) {
    // Out of the fall: standing and still, then placed as a teleport places, so the health, meters and stamina stay.
    m_outOfWorld = false;
    m_airborne = false;
    m_jumping = false;
    m_landingPending = false;
    m_airborneUpdates = 0;
    m_blockedUpdates = 0;
    m_velocity = anim::Vec3{};
    // The start point is unsnapped and may lie at or under the surface, where the placement's ray from the point
    // itself finds nothing: creation's ray from 1 m above finds it, 0.01 above the hit.
    if (ground != nullptr) {
        const raycast::Ray ray{.origin = toMesh(anim::add(position, anim::Vec3{0.0F, 0.0F, kSnapAbove})),
                               .direction = raycast::kDown,
                               .length = kSpawnLength};
        if (const auto hit = ground->rayCast(ray, {}, 0); hit) {
            position.z = position.z + kSnapAbove - hit->t + kSpawnGap;
        }
    }
    place(nullptr, position, headingDegrees);
    resetMotion();
}

void Human::locomote(bool gated) {
    // A human no pad drives follows its brain by the AI's own rules.
    if (m_moveSpeed.has_value() && m_record.move.has_value() && !m_record.padDriven) {
        aiLocomote(gated);
        return;
    }
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
    float newSpeed = approachSpeed(current, target, m_stepSeconds);

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
    // (docs/research/ai-moving.md#moving).
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

void Human::aiLocomote(bool gated) {
    // The speed: a wounded human asked to move walks; otherwise the brain's, reached at the AI's rates
    // (docs/research/characters.md#ai-locomotion).
    constexpr float kMovingAsk = 0.1F;
    const Speeds& speeds = m_animator.speeds();
    const float current = ps2::length(m_velocity.x, m_velocity.y);
    // Called only with both set (update()).
    CONEY_ASSERT(m_moveSpeed.has_value());
    CONEY_ASSERT(m_record.move.has_value());
    const float asked = *m_moveSpeed;
    const bool wounded = m_script.wounded;
    const float target = wounded && asked > kMovingAsk ? speeds.walk : asked;
    const float newSpeed = aiApproachSpeed(current, target);
    // The facing turns to the brain's heading by a constant step (no ease), only while the brain asks for a speed (or
    // the human is wounded). At speed 0 the original turns more than 15° off with a turn clip (395-398); **Coney
    // stand-in**: those clips are not played, so the human turns on the spot at the standing word instead.
    constexpr float kTurnClipAngle = 15.0F * kPi / 180.0F;
    const float wanted = m_record.move->heading;
    const int boost = m_record.move->turnBoost;
    const float error = wrapAngle(wanted - m_heading);
    float limit = 0.0F;
    if (asked > 0.0F || wounded) {
        limit = aiMaxTurn(gaitOfSpeed(current, speeds), boost, wounded);
    } else if (std::fabs(error) > kTurnClipAngle) {
        limit = aiMaxTurn(Gait::Standing, boost, wounded);
    }
    m_heading = std::fabs(error) <= limit ? wanted : wrapAngle(m_heading + std::copysign(limit, error));
    m_turn = TurnState{};
    // The locomotion gate, as for the player: a clip moving the body, or a start clip not yet blending.
    const anim::Vec3 direction = facing(m_heading);
    const bool clipMoves = gated || !m_animator.gaitBlendPlaying();
    const float horizontal = clipMoves ? 0.0F : newSpeed;
    m_velocity = anim::Vec3{direction.x * horizontal, direction.y * horizontal, m_velocity.z};
}

void Human::wheelchairControl(bool locked) {
    // Per update (docs/research/characters.md#wheelchair): both shoulders push toward the sprint speed; one alone slows
    // a little and turns, more at low speed; neither lets it roll down; cross brakes on top.
    // @orig 0x002427e8 Human_WheelchairControl (unknown)
    constexpr float kPush = 0.5F;       // 0x005104d4
    constexpr float kDecay = 0.04F;     // 0x005104d8
    constexpr float kBrake = 0.5F;      // 0x005104dc
    constexpr float kTurnFloor = 0.03F; // 0x005104e0
    constexpr float kTurnSpan = 0.05F;  // 0x005104e4 (0.08) less the floor
    const bool left = (m_record.buttons & pad::kL1) != 0;
    const bool right = (m_record.buttons & pad::kR1) != 0;
    const float top = m_animator.speeds().sprint;
    float speed = std::hypot(m_velocity.x, m_velocity.y);
    float turn = 0.0F;
    if (left && right) {
        speed = std::min(speed + kPush, top);
    } else {
        speed = std::max(speed - kDecay, 0.0F);
        if (left != right) {
            const float share = top > 0.0F ? 1.0F - std::min(speed / top, 1.0F) : 1.0F;
            turn = (kTurnFloor + kTurnSpan * share) * (left ? 1.0F : -1.0F);
        }
    }
    if ((m_record.buttons & pad::kCross) != 0) {
        speed = std::max(speed - kBrake, 0.0F);
    }
    // **Coney stand-ins**: the start clip from a standstill (state code 5), the animation layer's flag 2 and event
    // groups above 4.5 m/s are not built; a locked human (the movement lock, an arrest, a hold) does not roll.
    m_heading = wrapAngle(m_heading + turn);
    const float rolled = locked ? 0.0F : speed;
    const anim::Vec3 direction = facing(m_heading);
    m_velocity = anim::Vec3{direction.x * rolled, direction.y * rolled, m_velocity.z};
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
    m_heading = wrapAngle(m_heading + root.turn * 30.0F * m_stepSeconds);
}

std::span<const std::uint8_t> Human::passThrough() const {
    if (m_climbRun && m_climbRun->over) {
        return kFenceMaterials;
    }
    return {};
}

Human::Swept Human::sweep(const raycast::CollisionMesh& mesh, anim::Vec3 velocity, anim::Vec3 push, bool meetOthers) {
    m_contacts.clear();
    const anim::Vec3 from = m_position;
    // A body that does not move meets nothing.
    const anim::Vec3 displacement = anim::add(anim::scale(velocity, m_stepSeconds), push);
    if (anim::length(displacement) <= 0.0F) {
        return Swept{.feet = from, .velocity = std::nullopt};
    }
    // The other humans' capsules first. **Coney's choice**: the original sorts the humans' and the walls' contacts of
    // a pass by fraction together; here the humans take their part of the move before the walls take theirs. Their
    // slides take the velocity's part into them too, as the resolution's code 1 does.
    const anim::Vec3 move = meetOthers ? meetHumans(from, displacement) : displacement;
    for (const anim::Vec3 normal : m_contacts) {
        physics::removeNormalPart(velocity, normal);
    }
    // The level, through the physics world's sweep (docs/research/physics-bodies.md#u3-sweep): the walking sphere
    // (0.4705 m for Rembrandt, about 0.35 m for an AI human, whose body +0x60 is 1.0) against the walls the move goes
    // into, 0.25 m tall or more. What the humans left of the move beyond the velocity's goes in as the pending
    // push-out, undivided. Only a player's body writes the slid velocity back (body flag 0x400).
    const float radius = walkingSphereRadius();
    WalkingOwner owner(mesh, from, m_airborne, m_fighter.inPair() || m_fighter.blocking(), passThrough());
    physics::Body body{.owner = &owner,
                       .flags = physics::kBodyPushUndivided | (m_profile.player ? physics::kBodyWriteVelocity : 0U),
                       .typeMask = physics::kTypeHuman,
                       .pushOut = anim::subtract(move, anim::scale(velocity, m_stepSeconds)),
                       .stuck = static_cast<int>(m_blockedUpdates),
                       .shape = physics::BodyShape{.type = physics::ShapeType::Capsule,
                                                   .radius = radius,
                                                   .enabled = true,
                                                   .footGap = bodyTuning().footGap,
                                                   .minWallHeight = bodyTuning().minWallHeight},
                       .scaleKind = physics::ContactScale::Human};
    owner.setBody(&body);
    physics::MeshWorld world(mesh, m_meshScratch);
    static_cast<void>(physics::sweepBody(m_stepSeconds, world, body, velocity));
    m_blockedUpdates = static_cast<std::uint32_t>(body.stuck);
    // A slide the sweep kept for the next move (contact flag 0x2) joins the pending push.
    m_pendingPush = anim::add(m_pendingPush, body.pushOut);
    Swept swept{.feet = owner.position(), .velocity = owner.written()};
    // A human met asks for the write-back as a wall does (a player's body only).
    if (m_profile.player && !m_contacts.empty() && !swept.velocity) {
        swept.velocity = velocity;
    }
    // Then the world objects' bodies the move goes into, each push sliding the sphere along the body (**Coney
    // stand-in** until the objects are bodies of the physics world, Unit 11). The pushes are swept through the level as
    // a push-out, so none leaves the body in a wall.
    if (m_objectPush == nullptr || !*m_objectPush) {
        return swept;
    }
    const float centreHeight = radius + bodyTuning().footGap;
    anim::Vec3 at = swept.feet;
    anim::Vec3 total{};
    const std::size_t humansMet = m_contacts.size();
    for (int pass = 0; pass < kSweepPasses; ++pass) {
        const std::optional<anim::Vec3> out =
            (*m_objectPush)(*this, anim::add(at, anim::Vec3{0.0F, 0.0F, centreHeight}), radius, move);
        if (!out || anim::length(*out) < kClear) {
            break;
        }
        m_contacts.push_back(anim::normalise(*out));
        total = anim::add(total, *out);
        at = anim::add(at, *out);
    }
    if (m_contacts.size() == humansMet) {
        return swept;
    }
    WalkingOwner pushed(mesh, swept.feet, m_airborne, true, passThrough());
    physics::Body pushBody = body;
    pushBody.owner = &pushed;
    pushBody.flags = physics::kBodyPushUndivided;
    pushBody.pushOut = total;
    pushed.setBody(&pushBody);
    static_cast<void>(physics::sweepBody(m_stepSeconds, world, pushBody, anim::Vec3{}));
    swept.feet = pushed.position();
    // A player's velocity slides along the objects as along the level.
    if (m_profile.player) {
        anim::Vec3 slid = swept.velocity.value_or(velocity);
        for (std::size_t i = humansMet; i < m_contacts.size(); ++i) {
            physics::removeNormalPart(slid, m_contacts[i]);
        }
        swept.velocity = slid;
    }
    return swept;
}

float Human::walkingSphereRadius() const {
    return m_profile.player ? playerWalkingRadius(m_scale) : aiWalkingRadius(m_scale);
}

float Human::walkingSphereCentreHeight() const { return walkingSphereRadius() + bodyTuning().footGap; }

float Human::airSphereRadius() const { return bodyTuning().airRadius * m_scale; }

UprightCapsule Human::uprightCapsule() const {
    // **Coney's choice**: the 2 m upright capsule the strikes take too, not posed from the head bone each time.
    return UprightCapsule{.feet = m_position, .top = kStrikeCapsuleHeight * m_scale, .radius = capsuleRadius()};
}

bool Human::blocksHumans() const {
    // The flag, which only a heal or the release from an arrest gives back; health out takes it away at once (the
    // update latches that, updateState()).
    return m_blockHumansFlag && !m_outOfWorld && !m_fighter.health().depleted();
}

bool Human::linkedWith(const Human& other) const {
    const Holdable* self = this;
    const Holdable* that = &other;
    return (m_fighter.held() != nullptr && m_fighter.held() == that) ||
           (other.m_fighter.held() != nullptr && other.m_fighter.held() == self) ||
           (m_uncuffing != nullptr && m_uncuffing == &other) ||
           (other.m_uncuffing != nullptr && other.m_uncuffing == this);
}

float Human::pushWeight() const {
    // Every link sets both humans' weight to 1e9, and so does an arrest.
    constexpr float kImmovable = 1e9F;
    return m_fighter.inPair() || m_script.arrested || m_uncuffing != nullptr ? kImmovable : 1.0F;
}

anim::Vec3 Human::meetHumans(anim::Vec3 from, anim::Vec3 move) {
    if (m_otherHumans.empty()) {
        return move;
    }
    // A charging capsule (its strike flag on) strikes the human it meets and only slides (code 1); every other walk
    // pushes him too (code 4). The strike itself is the capsule strike's test (testStrikes()).
    const bool striking = m_strikes.capsuleOn();
    UprightCapsule self = uprightCapsule();
    self.feet = from;
    anim::Vec3 at = from;
    anim::Vec3 left = move;
    for (int pass = 0; pass < kSweepPasses && anim::length(left) > kClear; ++pass) {
        // The nearest contact along what is left of the move.
        Human* met = nullptr;
        std::optional<CapsuleContact> first;
        for (Human* other : m_otherHumans) {
            if (other == nullptr || other == this || !other->blocksHumans() || linkedWith(*other)) {
                continue;
            }
            const auto contact = sweepCapsules(self, left, other->uprightCapsule());
            if (contact && (!first || contact->fraction < first->fraction)) {
                first = contact;
                met = other;
            }
        }
        if (!first) {
            break;
        }
        // Code 4: half of this pass's move into him goes into his pending push-out, over his weight, unless he is not
        // pushable (body flag 0x200).
        if (!striking && met->pushable()) {
            met->addPush(contactPush(left, first->normal, met->pushWeight()));
        }
        // Code 1: to the contact, then the rest without its part into him.
        at = anim::add(at, anim::scale(left, first->fraction));
        self.feet = at;
        anim::Vec3 rest = anim::scale(left, 1.0F - first->fraction);
        if (const float into = anim::dot(rest, first->normal); into < 0.0F) {
            rest = anim::subtract(rest, anim::scale(first->normal, into));
        }
        m_contacts.push_back(first->normal);
        left = rest;
    }
    return anim::add(anim::subtract(at, from), left);
}

void Human::pushOutInAir(const raycast::CollisionMesh& mesh, anim::Vec3 feet) {
    // A player's push-out sphere is 0.5 × scale, centred its radius plus 0.05 above the feet, out of the nearest wall
    // (docs/research/physics-bodies.md#u2-003477c0-physicsbody-pushoutofwalls); the push waits for the next move.
    const float radius = airSphereRadius();
    const physics::Sphere sphere{.centre = anim::add(feet, anim::Vec3{0.0F, 0.0F, radius + bodyTuning().footGap}),
                                 .radius = radius};
    if (const auto push = physics::pushOutOfWalls(mesh, sphere, m_meshScratch); push) {
        addPush(*push);
    }
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
        m_groundMaterial = hit->material;
        m_coveredGround = (hit->flags & kCoveredGroundFlag) != 0;
        m_groundFlags = hit->flags;
        return;
    }
    // Nothing within 0.5 m below the feet: the human starts to fall.
    m_groundFlags = 0;
    m_position = feet;
    m_airborne = true;
    m_airborneUpdates = 0;
}

void Human::land(anim::Vec3 feet) {
    m_landingPending = false;
    m_lastLandingSpeed = m_velocity.z;
    ++m_landings;
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
    // On the ground the velocity has no z: walking climbs only through the snap. The push-out other humans' walks
    // left goes with this move, on top of the velocity's (docs/research/physics.md#sweep).
    m_velocity.z = 0.0F;
    const anim::Vec3 push = std::exchange(m_pendingPush, anim::Vec3{});
    // A charge stopped dead against a wall stays stopped until its clip ends; only a push moves it.
    const bool charging = m_animator.animId() == static_cast<std::uint32_t>(combat::anim_id::kRunningAttackCharge);
    m_chargeStopped = m_chargeStopped && charging;
    if (m_chargeStopped) {
        m_velocity.x = 0.0F;
        m_velocity.y = 0.0F;
        snapToGround(mesh, sweep(mesh, anim::Vec3{}, push, true).feet);
        return;
    }
    // A slope slows the human, uphill and downhill alike: the sweep is given the scaled velocity.
    const float factor = slopeFactor(m_groundNormal.z);
    const anim::Vec3 velocity{m_velocity.x * factor, m_velocity.y * factor, 0.0F};
    const Swept moved = sweep(mesh, velocity, push, true);
    anim::Vec3 feet = moved.feet;
    const anim::Vec3 displacement = anim::add(anim::scale(velocity, m_stepSeconds), push);
    const anim::Vec3 went = anim::subtract(moved.feet, m_position);
    if (charging && anim::length(displacement) > 0.0F &&
        anim::length(went) < kChargeStopShare * anim::length(displacement)) {
        // The charge met a wall head-on: it goes on to the wall along its own line, with no slide, and stops.
        const anim::Vec3 along = anim::normalise(displacement);
        feet = anim::add(m_position, anim::scale(along, std::max(0.0F, anim::dot(went, along))));
        m_velocity.x = 0.0F;
        m_velocity.y = 0.0F;
        m_chargeStopped = true;
    } else if (moved.velocity) {
        // The velocity the sweep wrote back: slid along what it met (a player's), or stopped when stuck. **Coney's
        // reading**: the sweep was given the slope-scaled velocity, so the scale is taken off again (the page does not
        // say which one the original writes back). Its z (a slide up a face that leans back) goes with the velocity
        // until the next update on the ground sets it to 0 (docs/research/characters.md#slopes, step 5).
        m_velocity.x = factor > 0.0F ? moved.velocity->x / factor : 0.0F;
        m_velocity.y = factor > 0.0F ? moved.velocity->y / factor : 0.0F;
        m_velocity.z = moved.velocity->z;
    }
    snapToGround(mesh, feet);
}

void Human::moveInAir(const raycast::CollisionMesh& mesh) {
    const anim::Vec3 push = std::exchange(m_pendingPush, anim::Vec3{});
    const anim::Vec3 displacement = anim::add(anim::scale(m_velocity, m_stepSeconds), anim::Vec3{push.x, push.y, 0.0F});
    const anim::Vec3 across{m_position.x + displacement.x, m_position.y + displacement.y, m_position.z};
    // The airborne body breaks the panes it reaches first, so a pane broken now no longer blocks the sweep
    // (docs/research/objects.md#pane-break). **Coney stand-in**: the push-out sphere stands for both the body and the
    // jump's strike shapes, whose sizes are not traced.
    if (m_bodyContact != nullptr && *m_bodyContact) {
        const float radius = airSphereRadius();
        (*m_bodyContact)(*this, anim::Vec3{across.x, across.y, across.z + radius + bodyTuning().footGap}, radius);
    }
    // The walls take the move across the ground; airborne, the sweep makes no corner slide and a player keeps a
    // quarter of the speed it slid with (the human body's contact scale,
    // docs/research/physics-bodies.md#u3-human-scale).
    // **Coney's reading**: the fall itself goes straight down and lands by the segment below, not by the sweep's
    // landing contact (#369).
    const Swept moved =
        sweep(mesh, anim::Vec3{m_velocity.x, m_velocity.y, 0.0F}, anim::Vec3{push.x, push.y, 0.0F}, false);
    if (moved.velocity) {
        m_velocity.x = moved.velocity->x;
        m_velocity.y = moved.velocity->y;
    }
    const anim::Vec3 feet{moved.feet.x, moved.feet.y, m_position.z + displacement.z};
    // After the move, the push out of the walls the body overlaps, for the next move (the owner's after-move call).
    pushOutInAir(mesh, feet);
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
            m_groundMaterial = hit->material;
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
    // held in a fight stance, docs/research/combat/index.md).
    if (m_fighter.blocking() && m_intent.magnitude > locomotionTuning().stickDeadZone) {
        m_heading = turnToward(m_heading, m_intent.angle - kPi / 2.0F, stanceTurn(), m_turn);
    }
    // A standing grab: the stick near full turns the grabber's back to it and walks the pair backward.
    const anim::Vec3 pull = m_fighter.moveGrab(m_intent.angle - kPi / 2.0F, m_intent.magnitude, m_animator, m_heading);
    if (anim::length(pull) > 0.0F) {
        m_velocity = anim::Vec3{pull.x, pull.y, m_velocity.z};
    }
}

void Human::aiCombatWalk(bool gated) {
    // The brain's move this update; none is speed 0 along the facing (Brain_StopMove's), so no turn and no step.
    constexpr float kMovingAsk = 0.1F;
    constexpr float kTurnClipAngle = 15.0F * kPi / 180.0F;
    const bool moving = m_moveSpeed.has_value() && m_record.move.has_value();
    const float asked = moving ? *m_moveSpeed : 0.0F;
    const float wanted = moving ? m_record.move->heading : m_heading;
    const bool wounded = m_script.wounded;
    // The turn to the brain's heading: whenever it asks for a speed (or the human is wounded, at the wounded rate) at
    // the AI's stance word; at speed 0 and more than 15° off, the turn on the spot (**Coney stand-in**: its stance
    // clips are not played, so the human turns at the standing word, as aiLocomote() does).
    const float error = wrapAngle(wanted - m_heading);
    float limit = 0.0F;
    if (asked > 0.0F || wounded) {
        limit = aiStanceTurn(wounded);
    } else if (moving && std::fabs(error) > kTurnClipAngle) {
        limit = aiMaxTurn(Gait::Standing, m_record.move->turnBoost, false);
    }
    m_heading = std::fabs(error) <= limit ? wanted : wrapAngle(m_heading + std::copysign(limit, error));
    m_turn = TurnState{};
    if (gated) {
        m_velocity = anim::Vec3{0.0F, 0.0F, m_velocity.z};
        return;
    }
    // The speed: the stance walk when asked for more than 0.1, else 0; wounded comes first, the walk speed.
    float speed = 0.0F;
    if (wounded) {
        speed = asked > kMovingAsk ? m_animator.speeds().walk : asked;
    } else if (asked > kMovingAsk) {
        speed = combat::combatTuning().combatWalkSpeed;
    }
    // The feet strafe along the line to the move's point, whatever the facing; on the point, they stand.
    const anim::Vec3 to = moving ? anim::subtract(m_record.move->aim, m_position) : anim::Vec3{};
    const float distance = std::hypot(to.x, to.y);
    const float newSpeed = distance > 1e-4F ? aiApproachSpeed(ps2::length(m_velocity.x, m_velocity.y), speed) : 0.0F;
    if (newSpeed <= 0.0F) {
        m_velocity = anim::Vec3{0.0F, 0.0F, m_velocity.z};
        m_animator.playCombatWalk(kAnimFightIdle);
        return;
    }
    m_velocity = anim::Vec3{to.x / distance * newSpeed, to.y / distance * newSpeed, m_velocity.z};
    // The clip by the movement's angle from the facing, clockwise (record +0xdc).
    const float clockwise = -wrapAngle(headingOf(to) - m_heading) * 180.0F / kPi;
    m_animator.playCombatWalk(static_cast<std::uint32_t>(combat::combatWalkClip(clockwise)));
}

void Human::combatWalk(const Combatant& target, bool gated) {
    // A human no pad drives runs the AI's stance control: its brain's move, never the target, turns it
    // (docs/research/ai.md#fight-facing).
    if (!m_record.padDriven) {
        aiCombatWalk(gated);
        return;
    }
    // The player faces the target every update.
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

void Human::fight(std::span<Combatant* const> targets, const raycast::CollisionMesh* mesh) {
    // The stick in the facing frame: x to the player's right, y ahead.
    const float relative = wrapAngle(m_intent.angle - kPi / 2.0F - m_heading);
    const combat::Stick stick{-m_intent.magnitude * std::sin(relative), m_intent.magnitude * std::cos(relative)};
    // Game time from the updates stepped (whole milliseconds, as the original keeps it).
    const std::uint64_t nowMs = m_updates * 1000 / 30;
    // **Coney's reading**: the dispatcher's gait tests (`0x00223a30`-`0x00223a60`) read the gait the last update's
    // velocity left, not this one's: in the original a square one update after the stick is first pushed fully still
    // snaps, though the run start moves the body on that update (docs/research/combat/index.md#attacks).
    m_fighter.update(FighterInput{.command = m_record.command,
                                  .buttons = m_record.buttons,
                                  .stick = stick,
                                  .padStick = combat::Stick{m_record.stickX, m_record.stickY},
                                  .gait = m_gaitBefore,
                                  .position = m_position,
                                  .heading = m_heading,
                                  .nowMs = nowMs,
                                  .targets = targets,
                                  .stepSeconds = m_stepSeconds,
                                  .objects = m_objectTargets,
                                  .strikeShapes = m_skeleton != nullptr,
                                  .mesh = mesh,
                                  .sprinting = m_sprinting,
                                  .padControlled = m_record.padDriven},
                     m_animator, m_heading);
}

void Human::updateMeters(bool sprintHeld) {
    // The drain at the sprint gait (nothing while an action's clip plays), which ends the sprint when it empties; the
    // refill otherwise; then the sprint flag, cleared and set again while L2 is held and stamina lasts.
    const Gait gaitNow = gait();
    const bool busy = m_animator.drivingClipPlaying() || m_climbRun.has_value();
    if (m_stamina.drain(gaitNow, busy, m_stepSeconds)) {
        m_sprinting = false;
    }
    m_stamina.refill(RefillBlocks{.gait = gaitNow, .airborne = m_airborne, .sprintHeld = sprintHeld}, m_stepSeconds);
    // A tireless human's stamina is full again every update.
    if (hasFlag(flag::kTireless)) {
        m_stamina.fill();
    }
    m_sprinting = sprintAsked(sprintHeld, m_stamina.value());
}

bool Human::tryContextAction() {
    // The level decides what the press does, whatever is in hand; not during a pick-up.
    if (m_pickUp) {
        return false;
    }
    // A cuffed human to free (kind 0) before the level's records and objects.
    if (m_firstContextAction && m_firstContextAction(*this)) {
        return true;
    }
    return m_contextAction && m_contextAction(*this);
}

bool Human::startPickUp(double handle, anim::Vec3 point, std::uint32_t clip) {
    const anim::AnimClip* found = m_animator.clip(clip);
    if (found == nullptr) {
        return false;
    }
    // The time to the clip's first event, whatever its type; with none, its whole playing time.
    const float rate = m_animator.anims().rate(clip) > 0.0F ? m_animator.anims().rate(clip) : 1.0F;
    float seconds = found->duration / rate;
    for (const anim::ClipEvent& event : found->events) {
        seconds = std::min(seconds, static_cast<float>(event.frame) / anim::kClipFrameRate / rate);
    }
    const int updates = std::max(1, static_cast<int>(std::lround(seconds / m_stepSeconds)));
    const anim::Vec3 to = anim::subtract(point, m_position);
    const float turn = std::hypot(to.x, to.y) > 1e-4F ? wrapAngle(headingOf(to) - m_heading) : 0.0F;
    const std::array<std::uint32_t, 1> clips{clip};
    m_animator.playCombat(clips, kPickUpIdle, AnimState::Attack, world_objects::kPickupBlend,
                          HeldFlags{.held = combat::kPhaseGrabStart, .set = combat::kPhaseGrabStart});
    m_velocity = anim::Vec3{};
    m_pickUp = PickUpRun{
        .handle = handle, .clip = clip, .updatesLeft = updates, .turnStep = turn / static_cast<float>(updates)};
    return true;
}

void Human::turnOverClip(std::uint32_t clip, anim::Vec3 point, float share) {
    const anim::AnimClip* found = m_animator.clip(clip);
    if (found == nullptr) {
        return;
    }
    const float rate = m_animator.anims().rate(clip) > 0.0F ? m_animator.anims().rate(clip) : 1.0F;
    const int updates = std::max(1, static_cast<int>(std::lround(found->duration / rate * share / m_stepSeconds)));
    const anim::Vec3 to = anim::subtract(point, m_position);
    const float turn = std::hypot(to.x, to.y) > 1e-4F ? wrapAngle(headingOf(to) - m_heading) : 0.0F;
    m_clipTurn = ClipTurn{.clip = clip, .updatesLeft = updates, .turnStep = turn / static_cast<float>(updates)};
}

void Human::turnToFace(anim::Vec3 point, float seconds) {
    const int updates = std::max(1, static_cast<int>(std::lround(seconds / m_stepSeconds)));
    const anim::Vec3 to = anim::subtract(point, m_position);
    const float turn = std::hypot(to.x, to.y) > 1e-4F ? wrapAngle(headingOf(to) - m_heading) : 0.0F;
    m_clipTurn = ClipTurn{.clip = 0, .updatesLeft = updates, .turnStep = turn / static_cast<float>(updates)};
}

bool Human::enterStepControl(float heading, int boost) {
    if (m_stepRequest || m_fighter.grabbed() || m_fighter.holdAttached() || m_fighter.holdState().has_value()) {
        return false;
    }
    m_stepRequest = StepRequest{.heading = wrapAngle(heading), .boost = boost};
    return true;
}

bool Human::stepHeld() const {
    constexpr std::uint32_t kStepOrTurnHeld = 0x20080000;
    return (m_animator.flags() & kStepOrTurnHeld) != 0;
}

bool Human::idleUnderControl() const {
    return !m_outOfWorld && !m_airborne && !m_climbRun && !hasFlag(flag::kWheelchair) &&
           !m_fighter.holdsMovement(m_animator) && !m_fighter.grabbed() && !m_fighter.holdAttached() &&
           !m_fighter.holdState().has_value() && m_animator.flags() == 0;
}

bool Human::runStepControl() {
    // The control moves nothing itself: the clip's root motion does.
    m_velocity = anim::Vec3{0.0F, 0.0F, m_velocity.z};
    // Waits while the human is busy, held by combat, wounded, or anything is held on the record.
    if (stickBusy(gateInput()) || m_fighter.holdsMovement(m_animator) || m_script.wounded || m_animator.flags() != 0) {
        return true;
    }
    // The clip toward the heading, then the idle (the fight idle in the stance), holding the step bit while it
    // plays; it fades in over 0.2 s.
    constexpr float kStepFade = 0.2F;
    const bool stance = m_fighter.lockTarget() != nullptr;
    CONEY_ASSERT(m_stepRequest.has_value()); // run only with a step asked for
    const StepClip step = stepClipFor(m_stepRequest->heading, m_heading, stance, m_stepRequest->boost);
    m_stepRequest.reset();
    const anim::AnimClip* found = m_animator.anims().clip(step.clip);
    if (found == nullptr) {
        return true;
    }
    m_animator.playCombat(clips::one(step.clip), stance ? kAnimFightIdle : clips::kIdle, AnimState::Attack, kStepFade,
                          HeldFlags{.held = clips::kStepHeld, .set = clips::kStepHeld});
    // The turn to the end facing, at a constant rate over half the clip's play time; under 0.01 rad, none.
    constexpr float kMinTurn = 0.01F;
    const float turn = wrapAngle(step.endFacing - m_heading);
    if (std::fabs(turn) >= kMinTurn) {
        const float rate = m_animator.anims().rate(step.clip) > 0.0F ? m_animator.anims().rate(step.clip) : 1.0F;
        const int updates = std::max(1, static_cast<int>(std::lround(found->duration / rate * 0.5F / m_stepSeconds)));
        m_clipTurn =
            ClipTurn{.clip = step.clip, .updatesLeft = updates, .turnStep = turn / static_cast<float>(updates)};
    }
    return true;
}

bool Human::startTagSpray(anim::Vec3 point) {
    // The turn to the tag is spread over half the intro's playing time. **Coney's reading**: with no intro clip the
    // play time is 0, so he turns at once (the original still turns) and the caller starts the stick game.
    constexpr float kHalf = 0.5F;
    m_velocity = anim::Vec3{};
    if (m_animator.clip(clips::kTaggingIntro) == nullptr) {
        turnToFace(point, 0.0F);
        return false;
    }
    turnOverClip(clips::kTaggingIntro, point, kHalf);
    // 334 holds the busy bit 0x400 (Tag_StartSprayClips), so the stance's movement sets no velocity while it plays.
    constexpr std::uint32_t kSprayIntroBusy = 0x400;
    m_animator.playCombat(clips::one(clips::kTaggingIntro), clips::kTaggingLoop, AnimState::Hold, kCombatFade,
                          HeldFlags{.held = kSprayIntroBusy, .set = kSprayIntroBusy});
    return true;
}

void Human::restartTagSpray() {
    // 337, then the loop 335 again (Tag_RestartSprayClips); a body playing no spray clip is left alone. Its drop of a
    // set 4 or 6 weapon has nothing to do here: one was dropped as the spray began and nothing is picked up during it.
    if (!tagSprayPlaying() || m_animator.clip(clips::kTaggingRestart) == nullptr) {
        return;
    }
    m_animator.playCombat(clips::one(clips::kTaggingRestart), clips::kTaggingLoop, AnimState::Hold, kCombatFade);
}

void Human::startUncuff(Human& cuffed, float mashFactor) {
    // Attack_SteerToTarget(0.1, freer, cuffed, 325): the cuffed human's turn is cleared, then the freer is turned and
    // slid onto 325's place from him; 325's own root turn faces him after (docs/research/crimes.md#uncuff-pose).
    cuffed.m_fighter.clearSteer();
    m_clipTurn.reset();
    m_velocity = anim::Vec3{};
    m_fighter.steerUncuff(cuffed, m_position, m_heading, m_animator);
    // 325 after a 0.01 s blend, holding 0x2000000 (the mash's input closed until it ends), then 329 while the mash
    // runs.
    constexpr std::uint32_t kMashClosed = 0x2000000;
    m_animator.playCombat(clips::one(clips::kReleaseIntro), clips::kReleaseLoop, AnimState::Hold, kUncuffFade,
                          HeldFlags{.held = kMashClosed, .set = kMashClosed});
    // Uncuff_BeginMash: the two linked (one body group, push weight 1e9) and the mash (mode 1, meter 0).
    // **Coney stand-in**, not built (docs/research/crimes.md#uncuffing): Uncuff_BeginMash's exit from the fight stance
    // and the lock, its cancel of a throw's aim, its state flags and hint 19, and Uncuff_Start's action 0x15 and its
    // push spheres turned off. The freer's final heading is off by 325's unscaled root turn (#263).
    m_uncuffing = &cuffed;
    m_fighter.combat().startTheft(combat::TheftKind::Mash, nowMs(), 1.0F, mashFactor);
}

bool Human::uncuffPlaying() const {
    const std::uint32_t id = m_animator.animId();
    return id == clips::kReleaseIntro || id == clips::kReleaseLoop;
}

void Human::endUncuff(bool hit) {
    m_clipTurn.reset();
    m_uncuffing = nullptr;
    m_fighter.combat().abortTheft();
    // 332 after the mash, 331 after a hit; then the idle.
    m_animator.playCombat(clips::one(hit ? clips::kReleaseHitReact : clips::kReleaseEnd), clips::kIdle,
                          AnimState::Attack, kCombatFade);
}

void Human::playUncuffReact(const Human& freer) {
    m_velocity = anim::Vec3{};
    m_animator.playPaired(clips::one(clips::kReleaseIntroReact), freer.animator().anims(), clips::kReleaseLoopReact,
                          AnimState::Hold, kUncuffFade);
}

void Human::endUncuffReact(const Human& freer, bool freed) {
    if (freed) {
        m_animator.playPaired(clips::one(clips::kReleaseEndReact), freer.animator().anims(), clips::kIdle,
                              AnimState::Attack);
        return;
    }
    // Still cuffed: back to the arrest's loop.
    m_animator.playCombat(clips::kNoClips, clips::kArrestedIdle, AnimState::Hold);
}

bool Human::tagSprayLooping() const { return m_animator.animId() == clips::kTaggingLoop; }

bool Human::tagSprayPlaying() const {
    const std::uint32_t id = m_animator.animId();
    return id == clips::kTaggingIntro || id == clips::kTaggingLoop || id == clips::kTaggingRestart;
}

void Human::endTagSpray() {
    // A finished or abandoned spray ends through 336 (Tag_EndSprayClips), whose event 0x31 drops the can; the fight
    // stance is the lock on a target (the original tests the state word's 0x3).
    m_clipTurn.reset();
    if (tagSprayPlaying()) {
        m_animator.playTagEnd(m_fighter.lockTarget() != nullptr);
    }
}

void Human::startStereoTheft(anim::Vec3 point, float stageTurns) {
    const anim::Vec3 to = anim::subtract(point, m_position);
    if (std::hypot(to.x, to.y) > 1e-4F) {
        m_heading = headingOf(to);
    }
    m_velocity = anim::Vec3{};
    m_fighter.startStereoTheft(m_animator, nowMs(), stageTurns);
}

bool Human::stereoTheftPlaying() const {
    const std::uint32_t id = m_animator.animId();
    return id == clips::kStereoStealIntro || id == clips::kStereoStealLoop;
}

void Human::followPickUp() {
    if (!m_pickUp) {
        return;
    }
    // Something else took over the body (a hit): the pick-up is lost.
    if (m_animator.animId() != m_pickUp->clip) {
        m_pickUp.reset();
        return;
    }
    m_heading = wrapAngle(m_heading + m_pickUp->turnStep);
    if (--m_pickUp->updatesLeft <= 0) {
        m_pickedUp = m_pickUp->handle;
        m_pickUp.reset();
    }
}

bool Human::tryClimb(const raycast::CollisionMesh& mesh, anim::Vec3 direction) {
    // The forward rays reach further for a player at the run or sprint gait, who then climbs in the running form.
    const ClimbTuning& tuning = climbTuning();
    const Gait gaitNow = gait();
    // Only a human with flag 0x2 starts a climb from a run.
    const bool running = (gaitNow == Gait::Run || gaitNow == Gait::Sprint) && hasFlag(flag::kFastClimber);
    // The probes measure along a unit direction: a brain's request is the way to its waypoint, whose length would
    // scale the face's distance and so the reach test.
    const anim::Vec3 way = flatUnit(direction);
    const auto probe = probeClimb(mesh, m_position, way, running ? tuning.runningReach : tuning.reach, true);
    if (!probe) {
        return false;
    }
    // The face must be within the clips' reach.
    const std::uint32_t standingId = climbFirstClip(probe->kind, false);
    const std::uint32_t runningId = climbFirstClip(probe->kind, true);
    const float standingReach = clipReach(*m_animator.clip(standingId));
    const float runningReach = clipReach(*m_animator.clip(runningId));
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
    // `HuBlockJump` skips the jump altogether.
    if (hasFlag(flag::kBlockJump)) {
        return false;
    }
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

bool Human::launchJump(anim::Vec3 velocity) {
    if (m_airborne || m_climbRun || (m_animator.flags() & kAiJumpRefuseFlags) != 0) {
        return false;
    }
    m_velocity = velocity;
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
            const anim::Vec3 d = m_animator.clip(id)->displacement;
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
    // The gait the last update's velocity left, before this update moves the body: the dispatcher's gait tests read it
    // (fight()). It is taken here, in the first pass, so that step() and human::Humans, which runs the passes itself,
    // both keep it.
    m_gaitBefore = gait();
    // Nobody listens to its sounds: the hits' are dropped (the clips keep none).
    if (!m_reportSounds) {
        static_cast<void>(m_fighter.takeSounds());
    }
    if (m_outOfWorld) {
        return;
    }
    // The animation's step (a climb follows its clips). A climb's first clip runs during the move to the start point,
    // as the original installs it with that move (0x0023d2b8, docs/research/characters.md#climb). The clip's warning
    // events reach the target as they pass.
    const anim::AnimTask* before = m_animator.tasks().top();
    const std::uint32_t beforeId = before != nullptr ? before->animId() : 0;
    const float beforeTime = before != nullptr ? before->time() : 0.0F;
    m_animator.advance(m_stepSeconds);
    followClimb(mesh);
    followPickUp();
    // A clip's turn to a point (the spray's intro, the uncuffing's), a step each update while the clip plays.
    if (m_clipTurn) {
        m_heading = wrapAngle(m_heading + m_clipTurn->turnStep);
        if (--m_clipTurn->updatesLeft <= 0 || (m_clipTurn->clip != 0 && m_animator.animId() != m_clipTurn->clip)) {
            m_clipTurn.reset();
        }
    }
    // A stereo theft whose clips something else took over (a hit, a knock-down, death, a scene) ends with no outcome,
    // as the original's hit (`0x00268c50`) and leaving normal mode (`0x002325e0`) end the mini-game.
    if (m_fighter.combat().theft() && !stereoTheftPlaying()) {
        m_fighter.combat().abortTheft();
    }
    sendWarnings(before, beforeId, beforeTime);
    noteSlowMotion();
    noteStrikeEvents(before, beforeId, beforeTime);
    noteReleaseEvent(before, beforeId, beforeTime);
    noteActionEvent(before, beforeId, beforeTime);
    noteCarriedPropEvents(before, beforeId, beforeTime);
}

void Human::noteCarriedPropEvents(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime) {
    const anim::AnimTask* top = m_animator.tasks().top();
    if (top == nullptr || top->eventClip() == nullptr) {
        return;
    }
    // From the clip's start when it began this step (or another clip took the top, or it had not advanced yet: 59's
    // can is made on its frame 0), else from where it was; the events in their order in the clip.
    const bool same = top == before && top->animId() == beforeId && top->time() >= beforeTime && beforeTime > 0.0F;
    const int from = same ? anim::eventFrame(beforeTime) : -1;
    const int to = anim::eventFrame(top->time());
    for (const anim::ClipEvent& event : top->eventClip()->events) {
        if (event.frame <= from || event.frame > to) {
            continue;
        }
        switch (event.type) {
        case kEventPropCreate:
            // Made only when it carries none (`+0x348` empty); whether the type is one to carry (kind 5) is the
            // level's test, which has the object types.
            if (!m_carriedProp) {
                m_carriedProp =
                    CarriedProp{.typeName = propTypeName(event), .point = kCarryPoint, .place = std::nullopt};
            }
            break;
        case kEventPropPlace:
            // Message 0xb7: to the attach point in the word, at the event's position scaled as the human is.
            if (m_carriedProp) {
                m_carriedProp->point = event.word;
                m_carriedProp->place =
                    PropPlace{.position = anim::scale(event.position, m_scale), .rotation = event.rotation};
            }
            break;
        case kEventPropRemove:
            if (m_carriedProp && m_carriedProp->typeName == propTypeName(event)) {
                releaseCarriedProps();
            }
            break;
        default:
            break;
        }
    }
}

void Human::resetForJoin() {
    // In the original's order (scenes.md#join-reset): the throw's aim (step 1); the pair broken before the grab, the
    // wound and the stun end (step 2); the full normal mode's idle (step 4) and the carried props (step 3), which
    // touch different state, so their order does not matter here.
    setThrowAiming(false);
    breakPair();
    setWounded(false);
    setNormalMode(true);
    releaseCarriedProps();
}

void endAllRage(std::span<Human* const> humans) {
    for (Human* human : humans) {
        if (human != nullptr) {
            human->setRageMode(false);
        }
    }
}

void Human::noteReleaseEvent(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime) {
    constexpr std::uint16_t kReleaseEvent = 10;
    const anim::AnimTask* top = m_animator.tasks().top();
    if (top == nullptr || top->eventClip() == nullptr) {
        return;
    }
    // From the clip's start when it began this step (or another clip took the top), else from where it was.
    const bool same = top == before && top->animId() == beforeId && top->time() >= beforeTime;
    const int from = same ? anim::eventFrame(beforeTime) : -1;
    const int to = anim::eventFrame(top->time());
    for (const anim::ClipEvent& event : top->eventClip()->events) {
        if (event.type == kReleaseEvent && event.frame > from && event.frame <= to) {
            m_throwRelease = true;
        }
    }
}

void Human::noteActionEvent(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime) {
    constexpr std::uint16_t kActionEvent = 0x41;
    m_actionEvent.reset();
    const anim::AnimTask* top = m_animator.tasks().top();
    if (top == nullptr || top->eventClip() == nullptr) {
        return;
    }
    // From the clip's start when it began this step (or another clip took the top), else from where it was.
    const bool same = top == before && top->animId() == beforeId && top->time() >= beforeTime;
    const int from = same ? anim::eventFrame(beforeTime) : -1;
    const int to = anim::eventFrame(top->time());
    const bool passed = std::ranges::any_of(top->eventClip()->events, [&](const anim::ClipEvent& event) {
        return event.type == kActionEvent && event.frame > from && event.frame <= to;
    });
    if (passed) {
        m_actionEvent = top->animId();
    }
}

void Human::noteSlowMotion() {
    constexpr std::uint16_t kSlowMotionOn = 0x2e;
    constexpr std::uint16_t kSlowMotionOff = 0x2f;
    // The events this update's animation fired, in their order (a clip handing over fired up to its end first); the
    // last slow-motion one wins.
    m_slowMotionEvent.reset();
    for (const anim::ClipEvent& event : m_animator.tasks().firedEvents()) {
        if (event.type == kSlowMotionOn || event.type == kSlowMotionOff) {
            m_slowMotionEvent = event.type;
        }
    }
}

void Human::noteStrikeEvents(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime) {
    const anim::AnimTask* top = m_animator.tasks().top();
    if (m_strikes.anyOn() && (top == nullptr || top->animId() != m_strikeAnim)) {
        m_strikes.clear();
    }
    if (top == nullptr || top->eventClip() == nullptr) {
        return;
    }
    // From the clip's start when it began this step (or another clip took the top, or it had not advanced yet: a clip
    // started by the last actions pass, whose frame-0 events, such as S1's hand, are still to fire), else from where it
    // was.
    const bool same = top == before && top->animId() == beforeId && top->time() >= beforeTime && beforeTime > 0.0F;
    const int from = same ? anim::eventFrame(beforeTime) : -1;
    const int to = anim::eventFrame(top->time());
    for (const anim::ClipEvent& event : top->eventClip()->events) {
        const bool strike = event.type == kEventStrikeOn || event.type == kEventStrikeOff ||
                            event.type == kEventStrikeAllOn || event.type == kEventStrikeAllOff;
        if (strike && event.frame > from && event.frame <= to) {
            m_strikes.onEvent(event);
            m_strikeAnim = top->animId();
        }
    }
}

BodyPlacement Human::placement() const {
    return BodyPlacement{.feet = m_position, .heading = m_heading, .lean = m_lean, .scale = m_scale};
}

std::vector<PosedShape> Human::posedStrikeShapes(bool targets) const {
    if (m_skeleton == nullptr) {
        return {};
    }
    std::vector<StrikeShapeDef> defs;
    if (targets) {
        std::ranges::copy_if(strikeShapeDefs(), std::back_inserter(defs), &StrikeShapeDef::target);
    } else {
        defs = m_strikes.active();
    }
    // The shapes stand on the bones as the original's bone lookup hands them over: each position times the body
    // scale, the rotation as posed (the offsets, lengths and radii are scaled in poseStrikeShapes()).
    auto bones = anim::boneTransforms(*m_skeleton, pose());
    for (anim::Mat34& bone : bones) {
        bone.t = anim::scale(bone.t, m_scale);
    }
    std::vector<PosedShape> posed = poseStrikeShapes(defs, bones, placement());
    // The capsule, while its strike flag is on: upright, its radius round the 2 m from the feet up.
    if (!targets && m_strikes.capsuleOn()) {
        const float radius = kStrikeCapsuleRadius * m_scale;
        const float top = kStrikeCapsuleHeight * m_scale;
        posed.push_back(PosedShape{.a = anim::add(m_position, anim::Vec3{0.0F, 0.0F, radius}),
                                   .b = anim::add(m_position, anim::Vec3{0.0F, 0.0F, top - radius}),
                                   .radius = radius,
                                   .bone = -1});
    }
    return posed;
}

std::optional<anim::Vec3> Human::bodyPoint() const {
    if (m_skeleton == nullptr) {
        return std::nullopt;
    }
    constexpr std::size_t kHips = 2;
    return anim::scale(anim::boneTransforms(*m_skeleton, pose())[kHips].t, m_scale);
}

PosedShape Human::struckCapsule() const {
    // Upright from the feet to the head bone's height + 0.2 (never under 0.2), its radius 0.35, all times the scale;
    // the walking sweep's body factor is not applied.
    const float least = kCapsuleAboveHead * m_scale;
    const float height = std::max(ledPoint().z - m_position.z + least, least);
    return PosedShape{.a = m_position,
                      .b = anim::add(m_position, anim::Vec3{0.0F, 0.0F, height}),
                      .radius = kStrikeCapsuleRadius * m_scale,
                      .bone = -1};
}

anim::Vec3 Human::ledPoint() const {
    if (m_skeleton == nullptr) {
        return m_position;
    }
    // Bone 6 posed into the world, as the original composes human +0x500 (already times the human's scale) with the
    // world transform.
    auto bones = anim::boneTransforms(*m_skeleton, pose());
    auto& headBone = bones.at(static_cast<std::size_t>(kLedPointBone));
    headBone.t = anim::scale(headBone.t, m_scale);
    const std::array<StrikeShapeDef, 1> head{StrikeShapeDef{.bone = kLedPointBone, .offset = {}}};
    return poseStrikeShapes(head, bones, placement()).front().a;
}

void Human::testStrikes(std::span<Human* const> victims, const StrikeContact* contact) {
    if (!m_strikes.anyOn() || m_skeleton == nullptr || m_outOfWorld) {
        m_strikesBefore.clear();
        return;
    }
    const std::vector<PosedShape> shapes = posedStrikeShapes(false);
    // Where each shape was posed the update before; one just switched on stands where it is.
    std::vector<PosedShape> before = shapes;
    for (PosedShape& shape : before) {
        const auto found = std::ranges::find(m_strikesBefore, shape.bone, &PosedShape::bone);
        if (found != m_strikesBefore.end()) {
            shape = *found;
        }
    }
    m_strikesBefore = shapes;
    const int animId = static_cast<int>(m_animator.animId());
    // A clip with flag 0x10000 (gen_rage_sweep, gen_sweep) strikes the target's capsule, not its spine and head.
    const anim::AnimClip* clip = m_animator.clip(static_cast<std::uint32_t>(animId));
    const bool capsuleStrike = clip != nullptr && (clip->flags & anim::kClipCapsuleStrike) != 0;
    if (m_fighter.strikesWithShapes(animId)) {
        for (Human* victim : victims) {
            // A body is struck once while the shapes stay on; one far beyond any shape's reach is not posed; a thrown
            // body does not strike its thrower.
            const anim::Vec3 to = anim::subtract(victim->position(), m_position);
            const bool thrower = clips::isThrow(animId) && victim->m_fighter.lastThrown() == this;
            if (victim == this || thrower || m_strikes.struckHuman(victim) || victim->m_skeleton == nullptr ||
                victim->outOfWorld() || std::hypot(to.x, to.y) > kStrikeBroadReach) {
                continue;
            }
            bool met = false;
            if (capsuleStrike) {
                met = meetsCapsule(before, shapes, victim->struckCapsule());
            } else {
                const std::vector<PosedShape> body = victim->posedStrikeShapes(true);
                for (std::size_t i = 0; i < shapes.size() && !met; ++i) {
                    met = std::ranges::any_of(
                        body, [&](const PosedShape& part) { return sweptShapesMeet(before[i], shapes[i], part); });
                }
            }
            if (met) {
                m_strikes.markStruckHuman(victim);
                m_fighter.strikeContact(*victim, animId, m_position, m_updates * 1000 / 30, m_animator);
            }
        }
    }
    if (contact != nullptr && *contact) {
        (*contact)(*this, shapes);
    }
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
    // Health out takes BLOCKHUMANS away (`Human_OnHealthOut`) until a heal gives the body back (blocksHumans()).
    if (m_fighter.health().depleted()) {
        loseBlockHumans();
    }
    m_lastMagnitude = m_intent.magnitude;
    m_intent = stickIntent(m_record.stickX, m_record.stickY, m_record.cameraForward);
    // The stick's last angle and magnitude, kept while it is pushed and fading by 0.8 an update once it is let go.
    if (m_intent.magnitude > locomotionTuning().stickDeadZone) {
        m_lastStick = m_intent;
    } else {
        m_lastStick.magnitude *= kLastStickFadePerStep;
    }
    // Flying (the debug noclip) replaces the rest: no brain, no collision, no gravity.
    if (m_flight) {
        fly();
        return;
    }
    // A brain's move replaces the stick: its heading as the stick's direction, pushed fully while it moves.
    m_moveSpeed.reset();
    if (m_record.move.has_value()) {
        m_moveSpeed = std::max(0.0F, m_record.move->speed);
        m_intent =
            StickIntent{.angle = m_record.move->heading + kPi / 2.0F, .magnitude = *m_moveSpeed > 0.0F ? 1.0F : 0.0F};
    }
    // An arrested human is not moved by its stick or its brain (**Coney stand-in**, human/script_state.h), nor is one
    // a grab holds, nor one whose movement a script locked (human/human_flags.h), nor one working out.
    // In a wheelchair a pad-driven human's sticks are not read (wheelchairControl()). **Coney stand-in**: so the
    // locomotion's clips, which would move the body, do not start; the wheelchair's clips are not played.
    const bool wheelchair = hasFlag(flag::kWheelchair) && m_record.padDriven;
    if (wheelchair) {
        m_intent.magnitude = 0.0F;
        m_lastStick.magnitude = 0.0F;
    }
    const bool movementHeld = m_script.arrested || m_script.knockedOut || m_script.workingOut ||
                              m_fighter.holdState().has_value() || hasFlag(flag::kMovementLocked);
    if (movementHeld) {
        m_intent.magnitude = 0.0F;
        m_moveSpeed.reset();
    }
    if (m_outOfWorld) {
        return;
    }
    // Attached to its grabber, it stays where the grabber placed it (Holdable::place()); its clips do not move it.
    if (m_fighter.holdAttached()) {
        m_velocity = anim::Vec3{};
        m_lastTurn = 0.0F;
        m_pendingPush = anim::Vec3{};
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
        m_velocity = anim::Vec3{left.x / (updates * m_stepSeconds), left.y / (updates * m_stepSeconds), 0.0F};
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
    } else if (m_stepRequest && runStepControl()) {
        // The step control holds this update: it waits for the human to be free, or has just played the step.
    } else if (wheelchair) {
        // A pad-driven human in a wheelchair runs its control instead; a brain-driven one keeps its own.
        wheelchairControl(movementHeld);
    } else if (const GateInput gate = gateInput();
               m_fighter.holdsMovement(m_animator) || stickBusy(gate) || tagSprayPlaying()) {
        // The stick step is skipped: combat's states, the record's +0x08 makes the human busy, or a spray runs (the
        // stance's walk would put its own clip over the spray's, docs/research/scripting.md#level87-fixes, fix 2).
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
        const TurnAndSlideStep steer = m_fighter.takeSteer(m_stepSeconds);
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
            m_velocity.z = std::max(m_velocity.z - kGravity * m_stepSeconds, -kMaxFallSpeed);
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
        // No level: nothing is met, but a push still moves the body.
        m_position = anim::add(anim::add(m_position, anim::scale(m_velocity, m_stepSeconds)),
                               std::exchange(m_pendingPush, anim::Vec3{}));
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
    updateHiding();
    // An arrested human neither fights nor acts (**Coney stand-in**, human/script_state.h), nor does one working out:
    // its pad pumps and quits the workout instead.
    // While a spray runs nothing else of the actions does (`Human_UpdateActions` returns once `MiniGame_Update` has
    // run the spray, before the stance, the commands and the anim-state choice), so the spray's clips stay on.
    if (tagSprayPlaying()) {
        m_lean = leanStep(m_lean, m_lastTurn, speed(), gait());
        return;
    }
    if (!m_airborne && !m_climbRun && !m_script.arrested && !m_script.workingOut) {
        fight(targets, mesh);
    }
    if (m_record.actionPressed && !m_fighter.holdsMovement(m_animator) && !m_script.arrested && !m_script.workingOut) {
        tryActions(mesh, m_record.sprintHeld);
    }
    // A brain's climb over a route's climb leg: the player's own climb start, toward the waypoint.
    if (m_record.climbToward && mesh != nullptr && !m_airborne && !m_climbRun && !m_fighter.holdsMovement(m_animator) &&
        !m_script.arrested) {
        static_cast<void>(tryClimb(*mesh, *m_record.climbToward));
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
