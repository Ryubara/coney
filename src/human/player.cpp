// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/player.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "animation/anim_pose.h"
#include "camera/camera_lens.h"
#include "camera/camera_view.h"
#include "characters/character_list.h"
#include "characters/character_rig.h"
#include "combat/combat_tuning.h"
#include "human/script_state.h"

namespace coney::human {

namespace {

// Whether the left stick points more than 157.5° from up (pulled back toward the camera), which keeps the auto-follow
// rules off (`+0x474`, docs/research/camera.md#heading).
bool stickPulledBack(float x, float y) {
    constexpr float kCosBehind = -0.92388F; // cos 157.5°
    const float length = std::hypot(x, y);
    return length > 0.0F && y / length < kCosBehind;
}

// What the follow camera needs of the human after its update: where it is and faces, its stored gait (which the
// auto-follow rules and the sprint zoom read, docs/research/camera.md#heading), whether it is in the air, the stick,
// the nearest enemy's distance (none with no enemies), and for the combat camera whether a pad-controlled player holds
// L1 at a fight target and where that target will be in half a second (docs/research/camera.md#combat-camera).
camera::FollowTarget followTargetOf(const Human& human, const Pad& pad, std::optional<float> nearestEnemy,
                                    bool padControlled) {
    // L1 held at the fight target (record flags 0x8 and 0x4), not the stance's own lock-on, which also locks without
    // it.
    const Combatant* lock = padControlled && pad.held(pad::kL1) ? human.fighter().target() : nullptr;
    std::optional<anim::Vec3> enemy;
    if (lock != nullptr) {
        enemy = anim::add(lock->position(), anim::scale(lock->velocity(), 0.5F));
    }
    return camera::FollowTarget{.feet = human.position(),
                                .heading = human.heading(),
                                .gait = static_cast<std::uint8_t>(human.gait()),
                                .airborne = human.airborne(),
                                .stickBack = stickPulledBack(pad.leftX(), pad.leftY()),
                                .nearestEnemy = nearestEnemy,
                                .lockOn = lock != nullptr,
                                .lockHeld = padControlled && pad.held(pad::kL1),
                                .enemy = enemy,
                                .secondary = std::nullopt,
                                .secondaryRange = 0.0F};
}

} // namespace

std::optional<PlayerStart> researchedPlayerStart(std::string_view level) {
    if (level == "level99") {
        return PlayerStart{.position = anim::Vec3{-284.4F, 120.4F, 0.3F}, .headingDegrees = 0.0F};
    }
    return std::nullopt;
}

std::expected<std::unique_ptr<PlayerCharacter>, Error>
PlayerCharacter::load(const io::Wad& wad, const chunk::ChunkHandlerTable& table, std::string_view name) {
    // The character's own resources, through the Character List.
    auto list = characters::loadCharacterList(wad);
    if (!list) {
        return std::unexpected(std::move(list.error()));
    }
    auto assets = characters::loadCharacterAssets(wad, *list, table, name);
    if (!assets) {
        return std::unexpected(std::move(assets.error()));
    }
    // The generic clips that stand in for the default anim table.
    const std::string genericName = std::format("{}", characters::kGenericAnimDataHash);
    auto genericEntry = wad.lookup(genericName);
    if (!genericEntry) {
        return fail(ErrorCode::NotFound, std::format("the generic character data {} is not on the disc", genericName));
    }
    auto generic = characters::loadCharacterData(wad, **genericEntry, table);
    if (!generic) {
        return std::unexpected(std::move(generic.error()));
    }
    // The Anim Range List the character's moves take their damage and reach from.
    auto ranges = combat::AnimRangeList::parse(assets->data.rangeList());
    if (!ranges) {
        return std::unexpected(std::move(ranges.error()));
    }
    std::unique_ptr<PlayerCharacter> character(
        new PlayerCharacter(std::move(*assets), std::move(*generic), std::move(*ranges)));
    if (const std::size_t missing = HumanAnimator::clipsMissing(character->m_anims, AnimSlots::player());
        missing != 0) {
        return fail(ErrorCode::NotFound, std::format("{}: {} locomotion clips missing", name, missing));
    }
    return character;
}

PlayerCharacter::PlayerCharacter(characters::CharacterAssets assets, characters::CharacterData generic,
                                 combat::AnimRangeList ranges)
    : m_assets(std::move(assets)), m_generic(std::move(generic)), m_anims(m_assets.data, &m_generic),
      m_skeleton(characters::characterSkeleton(m_assets.model)), m_ranges(std::move(ranges)) {}

PlayerClass playerClassOf(const characters::CharacterTypes& types, int type) {
    std::optional<characters::PlayerTraits> traits = types.playerTraitsOf(type);
    if (!traits) {
        return {};
    }
    return PlayerClass{
        .damage = std::move(traits->damage), .damagePercent = traits->damagePercent, .powerClass = traits->powerClass};
}

Player::Player(const PlayerCharacter& character, const raycast::CollisionMesh* mesh, const PlayerStart& start,
               const PlayerClass& playerClass)
    : m_start(start), m_human(character.anims(), AnimSlots::player(), anim::referenceRotations(), kPlayerBodyScale,
                              &character.ranges(), playerClass.damage, playerClass.damagePercent),
      m_camera(start.position, 0.0F) {
    if (playerClass.powerClass) {
        m_human.setFighterProfile(FighterProfile{.player = true, .powerClass = *playerClass.powerClass});
    }
    m_human.setSkeleton(&character.skeleton());
    m_humans.add(m_human, true);
    m_human.spawn(mesh, start.position, start.headingDegrees);
    m_camera = camera::FollowCamera(m_human.position(), m_human.heading());
    m_current = capture();
    m_previous = m_current;
}

void Player::teleport(const raycast::CollisionMesh* mesh, const PlayerStart& start) {
    m_driven->spawn(mesh, start.position, start.headingDegrees);
    resetCamera();
}

void Player::resetCamera() {
    m_camera = camera::FollowCamera(m_driven->position(), m_driven->heading());
    // A jump, not a move: both snapshots hold the new state, so a render does not blend across it.
    m_current = capture();
    m_previous = m_current;
}

void Player::placeCamera(float distance, float viewHeading) {
    m_camera.place(m_driven->position(), distance, viewHeading);
    m_current = capture();
    m_previous = m_current;
}

PlayerSnapshot Player::capture() const {
    PlayerSnapshot snapshot{.feet = m_human.position(),
                            .heading = m_human.heading(),
                            .lean = m_human.lean(),
                            .pose = m_human.pose(),
                            .cameraEye = m_camera.position(),
                            .cameraTarget = m_camera.lookAt(),
                            .fieldOfView = m_camera.fieldOfView(),
                            .nearClip = m_camera.nearClip(),
                            .farClip = camera::kPlayerCameraLens.farClip};
    // With the manager, its current view: the target on the view direction, so a blend's slerped orientation shows.
    if (m_cameras != nullptr) {
        const camera::CameraView& view = m_cameras->view();
        const float ahead = std::max(anim::distance(view.position, view.lookAt), 1.0F);
        snapshot.cameraEye = view.position;
        snapshot.cameraTarget = anim::add(view.position, anim::scale(camera::viewForward(view), ahead));
        snapshot.cameraUp = camera::viewUp(view);
        snapshot.cameraCuts = m_cameras->cuts();
        snapshot.fieldOfView = view.fieldOfView;
        snapshot.nearClip = view.nearClip;
        snapshot.farClip = view.farClip;
    }
    return snapshot;
}

Player::~Player() { setCameras(nullptr); }

void Player::setCameras(camera::Cameras* cameras) {
    if (m_cameras != nullptr) {
        m_cameras->attachFollow(nullptr);
    }
    m_cameras = cameras;
    if (m_cameras != nullptr) {
        m_cameras->attachFollow(&m_camera);
    }
    m_current = capture();
    m_previous = m_current;
}

PlayerSnapshot interpolate(const PlayerSnapshot& previous, const PlayerSnapshot& current, float alpha) {
    // The ends exactly, which a blend at 0 or 1 would only round to.
    if (alpha >= 1.0F) {
        return current;
    }
    if (alpha <= 0.0F) {
        return previous;
    }
    // Across a cut the camera jumps, as the original's does.
    const PlayerSnapshot& from = previous.cameraCuts == current.cameraCuts ? previous : current;
    return PlayerSnapshot{.feet = anim::lerp(previous.feet, current.feet, alpha),
                          .heading =
                              wrapAngle(previous.heading + wrapAngle(current.heading - previous.heading) * alpha),
                          .lean = previous.lean + (current.lean - previous.lean) * alpha,
                          .pose = anim::blendPoses(previous.pose, current.pose, alpha),
                          .cameraEye = anim::lerp(from.cameraEye, current.cameraEye, alpha),
                          .cameraTarget = anim::lerp(from.cameraTarget, current.cameraTarget, alpha),
                          .cameraUp = anim::lerp(from.cameraUp, current.cameraUp, alpha),
                          .cameraCuts = current.cameraCuts,
                          .fieldOfView = current.fieldOfView,
                          .nearClip = current.nearClip,
                          .farClip = current.farClip};
}

void Player::setPadControlled(bool padControlled) {
    m_padControlled = padControlled;
    m_humans.setPadControlled(*m_driven, padControlled);
}

void Player::drive(Human& human) {
    if (&human == m_driven) {
        return;
    }
    // The human left behind keeps only what its brain writes; the new one takes the pad as it stands, with no
    // command history carried over.
    m_humans.setPadControlled(*m_driven, false);
    PlayerRecord left;
    left.cameraForward = m_driven->record().cameraForward;
    m_driven->record() = left;
    m_driven = &human;
    m_humans.setPadControlled(*m_driven, m_padControlled);
    m_matcher = combat::CommandMatcher{};
}

void Player::update(const Pad& pad, const raycast::CollisionMesh* mesh, std::span<Combatant* const> targets) {
    // The command for this sample (docs/research/combat.md#commands).
    // The buttons are matched even with the pad locked; a locked pad's human does not act on them (HuLockPad), and a
    // command disabled for it is kept pending, not acted on (EnableCommand). Both still reach the pad handler
    // (docs/references/bindings/input.md#padsethandlerex).
    const ScriptState& script = m_driven->script();
    const std::uint16_t buttons = script.padLocked ? std::uint16_t{0} : pad.buttons();
    const combat::CommandId matched =
        m_matcher.update(pad.buttons(), m_tables, combat::combatTuning().historyHoldSamples, script.disabledCommands);
    const combat::CommandId command = script.padLocked ? combat::command::kNone : matched;
    const combat::CommandId padCommand = matched != combat::command::kNone ? matched : m_matcher.pending();
    // The pad into the human's per-player record, its stick turned by the camera as it stood after the last update;
    // L2 held asks for a sprint; triangle pressed (command 10) climbs or jumps (docs/research/characters.md#buttons).
    // Then the characters' step, and the cameras last.
    // Without the pad the record keeps only the brain's move, which the brains write in the step.
    const anim::Vec3 cameraForward = m_cameras != nullptr ? camera::viewForward(m_cameras->view()) : m_camera.forward();
    if (m_padControlled) {
        // A locked stick reads as centred (HuLockPadMovement); the buttons still act.
        const bool stickFree = !script.movementLocked;
        m_driven->record() = PlayerRecord{.stickX = stickFree ? pad.leftX() : 0.0F,
                                          .stickY = stickFree ? pad.leftY() : 0.0F,
                                          .cameraForward = cameraForward,
                                          .sprintHeld = !script.padLocked && pad.held(pad::kL2),
                                          .actionPressed = !script.padLocked && pad.pressed(pad::kTriangle),
                                          .command = command,
                                          .climbToward = std::nullopt,
                                          .padCommand = padCommand,
                                          .buttons = buttons,
                                          .padDriven = false,
                                          .move = std::nullopt};
    } else {
        // Only the view and the brain's move survive; the pad's fields read as released.
        PlayerRecord idle;
        idle.cameraForward = cameraForward;
        idle.move = m_driven->record().move;
        m_driven->record() = idle;
    }
    // The characters' step: 1/30 s, or slow motion's share of it while a slow-motion event holds it.
    m_humans.update(mesh, targets, m_cameras != nullptr ? m_cameras->slowMotion().stepSeconds() : kStepSeconds);
    // The step's shakes on player 1's camera (docs/research/camera.md#shake): a reaction to his hit, a reaction of his
    // own from strength 2, and his rage starting (level 1).
    if (m_cameras != nullptr) {
        for (const Human* human : m_humans.humans()) {
            const std::optional<ReactionShake>& shake = human->fighter().reactionShake();
            if (shake && (shake->attackerIsPlayer || (human == m_driven && shake->level >= 2))) {
                m_cameras->shake(shake->level);
            }
        }
        if (m_driven->fighter().rageStarted()) {
            m_cameras->shake(1);
        }
    }
    // A slow-motion event on the player's clip (docs/research/camera.md#slow-motion).
    if (const std::optional<std::uint16_t> event = m_driven->slowMotionEvent(); event && m_cameras != nullptr) {
        m_cameras->slowMotion().event(*event, 0);
    }
    if (m_driven->outOfWorld()) {
        // Put back, the camera reset behind him with its configuration kept.
        m_driven->spawn(mesh, m_start.position, m_start.headingDegrees);
        m_camera.observe(followTargetOf(*m_driven, pad, m_nearestEnemy, m_padControlled));
        m_camera.reset();
        ++m_respawns;
    }
    const auto& raw = pad.rawSticks(); // right x, right y, left x, left y
    const camera::FollowTarget after = followTargetOf(*m_driven, pad, m_nearestEnemy, m_padControlled);
    if (m_cameras != nullptr) {
        m_cameras->update(after, raw[0], raw[1], mesh, kStepSeconds);
    } else {
        m_camera.update(after, raw[0], raw[1], mesh, kStepSeconds);
    }
    // What drawing will read: this step's state, and the last one's to interpolate from.
    m_previous = m_current;
    m_current = capture();
}

} // namespace coney::human
