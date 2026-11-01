// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/game_tunables.h"

#include "camera/follow_camera.h"
#include "core/display_preset.h"
#include "human/body.h"
#include "human/climb.h"
#include "human/jump.h"
#include "human/locomotion.h"
#include "human/stamina.h"
#include "scenes/letterbox.h"

namespace coney::debug {

void registerGameTunables(TunableRegistry& registry) {
    // The player's locomotion: read every update.
    human::LocomotionTuning& move = human::locomotionTuning();
    registry.add("Movement", "Stick dead zone", &move.stickDeadZone)
        .range(0, 0.9, 0.01)
        .describe("Stick length at or below which the player stands");
    registry.add("Movement", "Run threshold", &move.runThreshold)
        .range(0.1, 1, 0.01)
        .describe("Stick length above which the player runs instead of walking");
    registry.add("Movement", "Acceleration", &move.acceleration)
        .range(0, 120, 1)
        .units("m/s2")
        .describe("Speed gained each second while speeding up (0.8 m/s an update)");
    registry.add("Movement", "Turn walking", &move.walkTurnDegrees).range(0, 45, 0.5).units("deg/step");
    registry.add("Movement", "Turn jogging", &move.jogTurnDegrees).range(0, 45, 0.5).units("deg/step");
    registry.add("Movement", "Turn running", &move.runTurnDegrees).range(0, 45, 0.5).units("deg/step");
    registry.add("Movement", "Turn sprinting", &move.sprintTurnDegrees).range(0, 45, 0.5).units("deg/step");
    registry.add("Movement", "Turn in a stance", &move.stanceTurnDegrees).range(0, 45, 0.5).units("deg/step");
    registry.add("Movement", "Turn ease", &move.turnEaseError)
        .range(0.1, 3.2, 0.05)
        .units("rad")
        .describe("Heading error from which a turn goes at its full limit; less turns ease in");
    registry.add("Movement", "Turn carry", &move.turnCarry)
        .range(0, 1, 0.05)
        .describe("Share of the last update's turn step carried into this one");
    registry.add("Movement", "Lean factor", &move.leanFactor)
        .range(0, 4, 0.05)
        .describe("Lean asked for: the turn an update times this times the speed (jogging and faster)");
    registry.add("Movement", "Lean factor walking", &move.walkLeanFactor).range(0, 40, 0.5);
    // The player's gait speeds come from the clips' root motion (docs/research/characters.md#speed-classes), so each
    // is tuned as a scale on the clip's speed; the combat walk's speed is the Combat page's. Another human's speeds
    // are the Entity inspector's per-entity speed scale.
    registry.add("Movement", "Sneak speed scale", &move.sneakSpeedScale)
        .range(0, 10, 0.05)
        .describe("Multiplies the player's sneak speed (the clip's, 1.585 m/s for Rembrandt)");
    registry.add("Movement", "Walk speed scale", &move.walkSpeedScale)
        .range(0, 10, 0.05)
        .describe("Multiplies the player's walk speed (the clip's, 1.629 m/s for Rembrandt)");
    registry.add("Movement", "Jog speed scale", &move.jogSpeedScale)
        .range(0, 10, 0.05)
        .describe("Multiplies the player's jog speed (the clip's, 4.857 m/s for Rembrandt)");
    registry.add("Movement", "Run speed scale", &move.runSpeedScale)
        .range(0, 10, 0.05)
        .describe("Multiplies the player's run speed (the clip's, 7.801 m/s for Rembrandt)");
    registry.add("Movement", "Sprint speed scale", &move.sprintSpeedScale)
        .range(0, 10, 0.05)
        .describe("Multiplies the player's sprint speed (the clip's, 10.245 m/s for Rembrandt)");

    // The walking body: its sphere and the 0.25 m rule that is all the step-up there is.
    human::BodyTuning& body = human::bodyTuning();
    registry.add("Body", "Radius", &body.radius)
        .range(0.05, 1, 0.01)
        .units("m")
        .describe("The walking sphere's radius before the character's scale (0.97 for Rembrandt)");
    registry.add("Body", "Foot gap", &body.footGap)
        .range(0, 1, 0.01)
        .units("m")
        .describe("The walking sphere's bottom above the feet");
    registry.add("Body", "Step height", &body.minWallHeight)
        .range(0, 2, 0.01)
        .units("m")
        .describe("Wall faces less tall than this do not stop a walking body; the ground snap lifts it on");
    registry.add("Body", "Player factor", &body.playerFactor)
        .range(0.5, 2, 0.01)
        .describe("A player's body factor: his walking sphere is the radius times this times the scale twice "
                  "(0.4704 m)");
    registry.add("Body", "Air radius", &body.airRadius)
        .range(0.05, 1.5, 0.01)
        .units("m")
        .describe("The player's push-out sphere while airborne, before the scale");

    // Sprint: read every update. Stamina's maximum and refill are each human's power class's.
    human::StaminaTuning& stamina = human::staminaTuning();
    registry.add("Sprint", "Drain", &stamina.drainPerSecond).range(0, 200, 1).units("/s");

    // The jump.
    human::JumpTuning& jump = human::jumpTuning();
    registry.add("Jump", "Minimum speed", &jump.minSpeed).range(0, 15, 0.1).units("m/s");
    registry.add("Jump", "Up speed", &jump.upSpeed).range(0, 20, 0.1).units("m/s").describe("Vertical speed at launch");
    registry.add("Jump", "Climbable check", &jump.climbableCheck)
        .range(0, 15, 0.1)
        .units("m")
        .describe("A climbable wall this close ahead refuses the jump");
    registry.add("Jump", "Air turn", &jump.airTurnDegrees).range(0, 45, 0.5).units("deg/step");

    // The climbs: the probes and the windows that pick a fence, a wall or their short forms.
    human::ClimbTuning& climb = human::climbTuning();
    registry.add("Climb", "Low probe", &climb.lowProbe).range(0, 3, 0.01).units("m");
    registry.add("Climb", "High probe", &climb.highProbe).range(0, 4, 0.01).units("m");
    registry.add("Climb", "Reach", &climb.reach).range(0, 10, 0.1).units("m");
    registry.add("Climb", "Running reach", &climb.runningReach).range(0, 10, 0.1).units("m");
    registry.add("Climb", "Probe behind", &climb.behind).range(0, 2, 0.05).units("m");
    registry.add("Climb", "Fence top limit", &climb.fenceTopLimit).range(0, 2, 0.05).units("m");
    registry.add("Climb", "Fence ceiling", &climb.fenceCeiling).range(0, 6, 0.05).units("m");
    registry.add("Climb", "Wall window low", &climb.tallWindowLow).range(0, 6, 0.01).units("m");
    registry.add("Climb", "Wall window high", &climb.tallWindowHigh).range(0, 6, 0.01).units("m");
    registry.add("Climb", "Short wall window low", &climb.shortWindowLow).range(0, 6, 0.01).units("m");
    registry.add("Climb", "Short wall window high", &climb.shortWindowHigh).range(0, 6, 0.01).units("m");

    // The follow camera: the lag and collision are read every update; the band and pitch when a camera is placed.
    camera::FollowTuning& follow = camera::followTuning();
    registry.add("Follow camera", "Position lag", &follow.positionLag)
        .range(0.01, 1, 0.01)
        .describe("Share of the wanted move the camera covers each update");
    registry.add("Follow camera", "Collision margin", &follow.collisionMargin).range(0, 2, 0.05).units("m");
    registry.add("Follow camera", "Closest after collision", &follow.minCollisionDistance).range(0, 5, 0.1).units("m");
    registry.add("Follow camera", "Auto-centre", &follow.autoCentre)
        .describe("Coney's switch over every camera's Auto Adjust option: off, the slower default rule for all");
    registry.add("Follow camera", "One player camera", &follow.onePlayerCamera)
        .describe("0x0050b19c = 1 (one player): a sprint pulls the camera in to the minimum distance");
    camera::FollowSettings& placed = camera::followDefaults();
    registry.add("Follow camera", "Leash near", &placed.leashNear)
        .range(0.5, 20, 0.1)
        .units("m")
        .describe("Near edge of the distance band; applies to the next follow camera made (a level start)");
    registry.add("Follow camera", "Leash far", &placed.leashFar)
        .range(0.5, 20, 0.1)
        .units("m")
        .describe("Far edge of the distance band; applies to the next follow camera made (a level start)");
    registry.add("Follow camera", "Pitch", &placed.pitchDegrees)
        .range(-20, 60, 1)
        .units("deg")
        .describe("Target pitch; applies to the next follow camera made (a level start)");
    registry.add("Follow camera", "Look-at height", &placed.lookAtHeight)
        .range(0, 3, 0.05)
        .units("m")
        .describe("Look-at point above the player's feet; applies to the next follow camera made (a level start)");

    // Display: Coney's own render-side settings (docs/guides/enhancements.md#where-the-settings-live).
    registry.add("Display", "Cutscene letterbox", &scenes::letterboxSettings().drawn)
        .describe("Draw the black bars over cutscenes, as the original does; off plays them full screen");
    // Read before the renderer starts (savedRenderer()), so a change here applies at the next start.
    registry.add("Display", "Renderer", &rendererSetting())
        .range(0, 1, 1)
        .describe("0: librw's OpenGL 3.3 (the default); 1: SDL3's GPU API, which only clears and presents so far, "
                  "so a saved 1 starts OpenGL with a warning until it draws the menus. Applies at the next start; "
                  "--renderer overrides it, and test mode ignores it");
    // The display preset and the values it sets (docs/engine/renderer.md#presets-built): the settings settle between
    // frames (DisplaySettings::settle()), so a preset chosen here writes its values on the next frame and a value
    // changed after it shows Custom. Nothing draws them but the renderer yet: each help line names the issue that will.
    DisplaySettings& display = displaySettings();
    registry.add("Display", "Preset", &display.presetSetting())
        .range(0, kChoosablePresets, 1)
        .describe("0: PS2, today's picture (the default); 1: Xbox; 2: Enhanced; 3: High; 4: Custom, shown once a "
                  "value below differs from every preset's. Test mode draws PS2 whatever is set here");
    DisplayValues& values = display.values();
    registry.add("Display", "Lights", &values.lights)
        .range(0, 3, 1)
        .describe("0: per vertex at the PS2's caps; 1: the Xbox's caps; 2: per pixel; 3: clustered. Not drawn yet "
                  "(#1217, #1221)");
    registry.add("Display", "Shadows", &values.shadows)
        .range(0, 3, 1)
        .describe("0: no shadow map; 1: the Xbox's; 2: a fitted PCF map; 3: two cascades. Not drawn yet (#1220, "
                  "#1222)");
    registry.add("Display", "Blob shadow", &values.blobShadow)
        .describe("The PS2's blob under each human; the Xbox draws a shadow map instead. Not switchable yet (#1220)");
    registry.add("Display", "Per-pixel fog", &values.perPixelFog)
        .describe("The Xbox's linear fog per pixel in place of the PS2's. Not drawn yet (#1217)");
    registry.add("Display", "Gamma ramp", &values.gammaRamp)
        .describe("The level's Xbox gamma ramp and its brightness default. Not drawn yet (#1216)");
    registry.add("Display", "Reflections", &values.reflections)
        .range(0, 2, 1)
        .describe("0: none; 1: environment maps; 2: with GTAO and probes. Not drawn yet (realtime lighting design)");
    registry.add("Display", "Anti-aliasing", &values.antiAliasing)
        .range(0, 2, 1)
        .describe("0: none; 1: SMAA; 2: MSAA or TAA with SMAA. Not drawn yet (#1219, #1218, #1224)");
}

RendererKind savedRenderer(const std::string& path) {
    // A registry of its own with only this setting: the game's tunables do not exist yet, and the file's other lines
    // are kept for them by the full load after the start.
    TunableRegistry scratch;
    int value = 0;
    scratch.add("Display", "Renderer", &value).range(0, 1, 1);
    if (!scratch.load(path)) {
        return RendererKind::Gl3;
    }
    scratch.applyPending();
    return rendererOfSetting(value);
}

} // namespace coney::debug
