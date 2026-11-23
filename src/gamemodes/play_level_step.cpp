// SPDX-License-Identifier: GPL-3.0-or-later
// The level's step (PlayLevelSimulation::update()): the humans, the objects, the HUD's state and the game-over check in
// the original's order, with the presentation's hooks called where its parts of the step fall
// (docs/guides/network-architecture.md#sub-presentation); and the parts of the step that came out of the platform's
// mode with it: the scripts' clip replacements, the world objects brought in round the camera, the sandbox's targets.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <vector>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "animation/anim_math.h"
#include "camera/cameras.h"
#include "core/game_timer.h"
#include "core/pad.h"
#include "core/perf.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/hud_feed.h"
#include "gamemodes/play_level_simulation.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/target_human.h"
#include "raycast/collision_mesh.h"
#include "warriors/inventory.h"

namespace coney {

namespace {

// Game time in milliseconds from GameTimer ticks.
// @coney-only Coney's game clock in milliseconds
std::uint64_t millisecondsOf(std::uint64_t ticks) { return ticks / (GameTimer::kTicksPerSecond / 1000); }

// The player slot this mode's player plays in: player 1, the only player a level has in Coney yet. Stand-in for the
// player's own slot (networking checklist item 3): the mode holds one player and nothing in it carries a slot yet,
// so its pad, camera and options all mean player 1; giving the mode a player per slot is wider than the Options
// screen and comes with the second player.
constexpr std::size_t kPlayerSlot = 0;

// How far above a target's spot the drop onto the ground starts, and how far below it reaches, in metres (as the
// platform's teleport drops).
constexpr float kTargetDrop = 2.0F;

// A point in the game's axes in RenderWare's: the game's (x, y, z) is RenderWare's (x, z, -y).
// @coney-only Coney's change of axes for the step's camera
world::Vec3 renderWareOf(anim::Vec3 game) { return world::Vec3{game.x, game.z, -game.y}; }

} // namespace

ModeResult PlayLevelSimulation::update(GameModeStack& stack, const FrameTime& frame) {
    // `--freeze-world`, once its first step is done: nothing advances, and the presentation makes the step.
    if (stepFrozenWorld(frame)) {
        return ModeResult::Stay;
    }

    // The free camera, when on, takes pad 1 and the player stands still, as a frozen one does.
    const Pad& pad = stack.pads().port(0);
    if (m_freeCamera) {
        m_freeCamera->commit();
        m_freeCamera->current().update(pad, static_cast<float>(frame.seconds));
    }
    static const Pad kStill;
    // In the noclip the pad flies him (human::Player::update()) and works nothing else: no lock pick, throw aim or
    // mugging.
    const bool noclip = m_player->noclip();
    // While the debug menus' Animation player plays a clip on him, he stands still for it as a frozen player does.
    const bool held = m_frozen || m_player->human().animator().debugClipPlaying();
    const bool picking = !noclip && !held && !m_freeCamera && !sceneHoldsPlayer() && stepLockPick(pad);
    const Pad& playerPad = held || m_freeCamera || sceneHoldsPlayer() || picking ? kStill : pad;
    const Pad& actionPad = noclip ? kStill : playerPad;

    // The characters' update, then the cameras' (human::Player keeps that order).
    CONEY_PERF_ZONE("play.update");
    {
        CONEY_PERF_ZONE("play.anim_overrides");
        applyAnimOverrides();
    }
    const anim::Vec3 before = m_player->human().position();
    // The player's camera follows by its own pad's Auto Adjust option, which the Options screen may have changed
    // (Cam_Follow_AutoFollow reads the byte of the camera's pad each update, docs/research/pause.md#options).
    if (m_padOptions != nullptr) {
        m_player->camera().setAutoAdjust(m_padOptions->at(kPlayerSlot).autoAdjustCamera);
    }
    giveObjectTargets();
    stepObjectBodies();
    {
        CONEY_PERF_ZONE("play.humans");
        // The pads' sticks turned by the player's camera as the last update left it, then the characters' step.
        camera::PlayerStickCameras stickCameras = m_player->stickCameras();
        stack.turnSticks(stickCameras);
        m_player->update(playerPad, &m_scenery->collision(), m_combatants);
        for (Target& target : m_targets) {
            if (!target.frozen) {
                target.human->step();
            }
        }
    }
    {
        CONEY_PERF_ZONE("play.ai_capture");
        m_ai->capture();
    }
    stepPlacedHeld();
    stepPickups();
    stepAiThrows();
    stepThrowAim(actionPad);
    stepMugMeter(m_player->human(), actionPad);
    stepLockPickDial();
    stepLooseObjects();
    stepFlash();
    stepDealerPairs();
    stepGameOver(pad);
    stepObjects();
    stepHats();
    stepCarriedProps();
    const anim::Vec3 after = m_player->human().position();
    m_stats.travelled += std::hypot(after.x - before.x, after.y - before.y);
    traceStep();
    if (const std::uint32_t id = m_player->human().animator().animId(); id != m_lastAnimId) {
        m_print(std::format("frame {}: clip {}\n", frame.index, id));
        m_lastAnimId = id;
    }
    // The scenes, with any player's skip buttons.
    {
        CONEY_PERF_ZONE("play.scenes");
        stepScenes(millisecondsOf(frame.gameTicks),
                   static_cast<std::uint16_t>(pad.buttons() | stack.pads().port(1).buttons()));
    }

    // The presentation's scenery round the step's camera (its streaming, the lights' flicker and the visibility pass),
    // then the world objects round that camera, after the scripts and the scenes moved them.
    CONEY_PERF_ZONE("play.world");
    const StepView view = stepScenery(frame);
    stepWorldObjects(view, static_cast<std::uint32_t>(std::lround(frame.seconds * 1000.0)));
    recordDrawnPoses();
    // The HUD's step (docs/research/hud.md#the-huds-frame), each panel from its player slot's state: player 1's in slot
    // 0, made afresh each step; slot 1 has no player yet.
    hud::HudFrame hudFrame;
    hudFrame.nowMs = millisecondsOf(frame.gameTicks);
    hudFrame.pads.at(0) = &actionPad;
    // The fixed-camera icon: a locked camera (type 1) or switch 0 off ignores the right stick. Coney has no fixed (0),
    // transition (5) or rail (9) camera.
    hud::PlayerHudState& player1 = m_playerHuds.at(0);
    player1 = hud::PlayerHudState{};
    if (const camera::Cameras* cameras = m_cast.cameras; cameras != nullptr) {
        player1.cameraIgnoresStick =
            cameras->current().kind == camera::CameraKind::Locked || !cameras->enabled(camera::Cameras::kSwitchStick);
    }
    hudFrame.levelNumber = m_levelNumber;
    // The gang-count indicators' counts: the standing members, `Gang_CountLiving(gang, 1)`
    // (docs/research/hud-functions-lua.md#gang-count-what).
    if (m_ai) {
        hudFrame.gangLiving = ai::hudGangCount(m_ai->brains().gangs());
    }
    // A scene's letterbox in or moving hides the HUD.
    hudFrame.letterbox = letterboxShown(hudFrame.nowMs);
    // Player 1's rage meter and whether he is knocked out (`Human_IsKnockedOut`), which drops his Armies panel.
    feedHumanToHud(player1, m_player->human());
    // Player 1's radar: his feet, facing and speed, and the camera's facing in the humans' convention (0 faces +y,
    // positive to the left); RenderWare's (x, y, z) is the game's (x, -z, y).
    hudFrame.radar.known = true;
    hudFrame.radar.position = m_player->human().position();
    hudFrame.radar.heading = m_player->human().heading();
    hudFrame.radar.speed = m_player->human().speed();
    hudFrame.radar.cameraHeading = std::atan2(-view.pose.forward.x, -view.pose.forward.z);
    // The score, the money and the four item counters, which the original's panel reads from the stats object and the
    // inventory itself (docs/research/hud.md#item-counters).
    if (m_pickups != nullptr) {
        hud::PanelValues& values = player1.panel;
        values.score = m_pickups->score(0);
        values.money = m_pickups->carried(0, item::kMoney);
        // **Coney's stand-in** for the handcuff counter's `Human_GetCuffCount` (not traced): inventory item 5.
        values.items = {m_pickups->carried(0, item::kRevive), m_pickups->carried(0, item::kSprayPaint),
                        m_pickups->carried(0, item::kHandcuffs), m_pickups->carried(0, item::kHandcuffKey)};
    }
    for (std::size_t slot = 0; slot < m_playerHuds.size(); ++slot) {
        hud::applyPlayerHud(hudFrame, slot, m_playerHuds.at(slot));
    }
    // The crew status list's humans and player slot 0's gang (docs/research/hud-functions-parts.md#crew-status); slot 1
    // has no player yet. The list lives until the HUD's step below has read it.
    std::vector<hud::CrewMember> crew;
    if (m_ai) {
        crew = feedCrewToHud(m_ai->brains(), m_cast.humans, m_player->camera().position());
        hudFrame.crew.humans = crew;
        if (const ai::Brain* chief = m_ai->brains().find(m_player->human()); chief != nullptr && chief->gang()) {
            hudFrame.crew.playerGangs.at(0) = chief->gang()->id();
        }
    }
    m_hud->hud().update(hudFrame);
    hudStepped();
    // The health rings, after the HUD's step: hidden with it.
    stepRings(playerPad, hudFrame.nowMs);
    ++m_stats.frames;
    stepEnded(frame);
    return ModeResult::Stay;
}

PlayLevelSimulation::StepView PlayLevelSimulation::stepScenery(const FrameTime& /*frame*/) {
    // Player 1's camera of the newest step, in RenderWare's axes, kept upright; nothing streams without a presentation.
    const human::PlayerSnapshot& snapshot = m_player->current();
    const anim::Vec3 look = anim::subtract(snapshot.cameraTarget, snapshot.cameraEye);
    const anim::Vec3 forwardGame = anim::length(look) > 1e-6F ? anim::normalise(look) : anim::Vec3{0.0F, 1.0F, 0.0F};
    const world::Vec3 forward = renderWareOf(forwardGame);
    StepView view;
    view.pose.position = renderWareOf(snapshot.cameraEye);
    view.pose.forward = forward;
    view.drawDistance = snapshot.farClip;
    return view;
}

bool PlayLevelSimulation::sceneHoldsPlayer() const { return sceneHolds(m_playerHandle); }

void PlayLevelSimulation::stepWorldObjects(const StepView& view, std::uint32_t elapsedMs) {
    if (m_records == nullptr || m_objectTypes == nullptr) {
        return;
    }
    // RenderWare's axes back into the game's: (x, y, z) is the game's (x, -z, y).
    world_objects::ObjectTasks::StepView objects;
    objects.camera = anim::Vec3{view.pose.position.x, -view.pose.position.z, view.pose.position.y};
    objects.drawDistance = view.drawDistance;
    objects.elapsedMs = elapsedMs;
    // An object in a hand, a hat on a head or a carried prop is drawn by its holder.
    objects.inHand = [this](double handle) {
        return m_wornHats.contains(handle) || m_carriedProps.contains(handle) ||
               (m_pickups != nullptr && m_pickups->inHand(handle));
    };
    m_objectTasks.step(*m_records, *m_objectTypes, objects);
    // The glints, at 60 ticks a second of the step's game time.
    stepGlints(static_cast<int>(std::lround(static_cast<double>(elapsedMs) * 60.0 / 1000.0)));
}

void PlayLevelSimulation::applyAnimOverrides() {
    for (human::Human* human : m_player->humans().humans()) {
        const human::ScriptState& script = human->script();
        // The idle's replacement (HuUseAnim slot 0).
        const std::string wanted(script.animOverride(human::kUseAnimIds.front()));
        if (wanted != human->idleClipName()) {
            const anim::AnimClip* clip = m_dynamicClips.find(wanted);
            if (!wanted.empty()) {
                m_print(clip != nullptr ? std::format("anim: {} replaces a human's idle\n", wanted)
                                        : std::format("anim: {} not loaded; the idle plays\n", wanted));
            }
            human->setIdleClip(wanted, clip);
        }
        // Every other id's: each slot's clip put in when it changed, each one no longer in a slot taken out.
        for (const human::AnimOverride& slot : script.animOverrides) {
            if (slot.clip.empty() || slot.animId == human::kUseAnimIds.front()) {
                continue;
            }
            const auto applied = human->overrideClipNames().find(slot.animId);
            if (applied == human->overrideClipNames().end() || applied->second != slot.clip) {
                const anim::AnimClip* clip = m_dynamicClips.find(slot.clip);
                m_print(clip != nullptr ? std::format("anim: {} replaces a human's anim {}\n", slot.clip, slot.animId)
                                        : std::format("anim: {} not loaded; anim {} plays\n", slot.clip, slot.animId));
                human->setOverrideClip(slot.animId, slot.clip, clip);
            }
        }
        std::vector<std::uint32_t> gone;
        for (const auto& [id, name] : human->overrideClipNames()) {
            if (script.animOverride(id).empty()) {
                gone.push_back(id);
            }
        }
        for (const std::uint32_t id : gone) {
            human->setOverrideClip(id, {}, nullptr);
        }
    }
}

void PlayLevelSimulation::makeTargets() {
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    std::uint32_t seed = 1;
    for (const world::SceneTarget& point : m_scenery->targets()) {
        // Dropped onto the ground below its spot, as a teleport is.
        raycast::Vec3 feet{point.feet.x, point.feet.y, point.feet.z + kTargetDrop};
        if (!raycast::dropToGround(mesh, kTargetDrop * 2.0F, feet)) {
            feet.z = point.feet.z;
        }
        // **Coney's choice**: a target looks like the player (the one character Coney loads); its own model waits
        // for the character classes.
        Target target;
        target.human = std::make_unique<human::TargetHuman>(
            m_character->anims(), human::AnimSlots::player(), anim::referenceRotations(), point.health,
            anim::Vec3{feet.x, feet.y, feet.z}, point.headingDegrees * std::numbers::pi_v<float> / 180.0F, seed++);
        m_targetPointers.push_back(target.human.get());
        m_combatants.push_back(target.human.get());
        m_targets.push_back(std::move(target));
    }
    if (!m_targets.empty()) {
        m_print(std::format("targets: {} from the layout\n", m_targets.size()));
    }
}

} // namespace coney
