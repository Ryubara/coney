// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/player.h"

#include <format>
#include <string>
#include <utility>

#include "animation/anim_pose.h"
#include "characters/character_list.h"
#include "characters/character_rig.h"
#include "combat/combat_tuning.h"

namespace coney::human {

namespace {

// What the follow camera needs of the human after its update: where it is and faces, and whether the auto-centre rule
// and the sprint zoom follow it. The rule turned the camera while walking, running, sprinting and in the air, and not
// while standing or while a start or landing clip played (docs/research/camera.md#street); **Coney's choice** extends
// that to every clip that moves the body (the run stop, a climb, an attack).
camera::FollowTarget followTargetOf(const Human& human) {
    const Gait gait = human.gait();
    const bool moving = human.airborne() || gait != Gait::Standing;
    return camera::FollowTarget{.feet = human.position(),
                                .heading = human.heading(),
                                .turnsCamera = moving && !human.animator().drivingClipPlaying(),
                                .running = gait == Gait::Run || gait == Gait::Sprint,
                                .sprinting = gait == Gait::Sprint};
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

Player::Player(const PlayerCharacter& character, const raycast::CollisionMesh* mesh, const PlayerStart& start)
    : m_start(start), m_human(character.anims(), AnimSlots::player(), anim::referenceRotations(), kPlayerBodyScale,
                              &character.ranges()),
      m_camera(start.position, 0.0F) {
    m_human.spawn(mesh, start.position, start.headingDegrees);
    m_camera = camera::FollowCamera(m_human.position(), m_human.heading());
    m_current = capture();
    m_previous = m_current;
}

void Player::teleport(const raycast::CollisionMesh* mesh, const PlayerStart& start) {
    m_human.spawn(mesh, start.position, start.headingDegrees);
    resetCamera();
}

void Player::resetCamera() {
    m_camera = camera::FollowCamera(m_human.position(), m_human.heading());
    // A jump, not a move: both snapshots hold the new state, so a render does not blend across it.
    m_current = capture();
    m_previous = m_current;
}

PlayerSnapshot Player::capture() const {
    return PlayerSnapshot{.feet = m_human.position(),
                          .heading = m_human.heading(),
                          .lean = m_human.lean(),
                          .pose = m_human.pose(),
                          .cameraEye = m_camera.position(),
                          .cameraTarget = m_camera.lookAt()};
}

PlayerSnapshot interpolate(const PlayerSnapshot& previous, const PlayerSnapshot& current, float alpha) {
    // The ends exactly, which a blend at 0 or 1 would only round to.
    if (alpha >= 1.0F) {
        return current;
    }
    if (alpha <= 0.0F) {
        return previous;
    }
    return PlayerSnapshot{.feet = anim::lerp(previous.feet, current.feet, alpha),
                          .heading =
                              wrapAngle(previous.heading + wrapAngle(current.heading - previous.heading) * alpha),
                          .lean = previous.lean + (current.lean - previous.lean) * alpha,
                          .pose = anim::blendPoses(previous.pose, current.pose, alpha),
                          .cameraEye = anim::lerp(previous.cameraEye, current.cameraEye, alpha),
                          .cameraTarget = anim::lerp(previous.cameraTarget, current.cameraTarget, alpha)};
}

void Player::update(const Pad& pad, const raycast::CollisionMesh* mesh, std::span<TargetHuman* const> targets) {
    // The command for this sample (docs/research/combat.md#commands).
    const combat::CommandId command =
        m_matcher.update(pad.buttons(), m_tables, combat::combatTuning().historyHoldSamples);
    // The human first, its stick turned by the camera as it stood after the last update; the cameras last.
    // L2 held asks for a sprint; triangle pressed (command 10) climbs or jumps (docs/research/characters.md#buttons).
    m_human.step(HumanInput{.stickX = pad.leftX(),
                            .stickY = pad.leftY(),
                            .cameraForward = m_camera.forward(),
                            .sprintHeld = pad.held(pad::kL2),
                            .actionPressed = pad.pressed(pad::kTriangle),
                            .command = command,
                            .buttons = pad.buttons(),
                            .targets = targets},
                 mesh);
    if (m_human.outOfWorld()) {
        m_human.spawn(mesh, m_start.position, m_start.headingDegrees);
        m_camera = camera::FollowCamera(m_human.position(), m_human.heading());
        ++m_respawns;
    }
    const auto& raw = pad.rawSticks(); // right x, right y, left x, left y
    m_camera.update(followTargetOf(m_human), raw[0], raw[1], mesh, kStepSeconds);
    // What drawing will read: this step's state, and the last one's to interpolate from.
    m_previous = m_current;
    m_current = capture();
}

} // namespace coney::human
