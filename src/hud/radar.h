// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "animation/anim_math.h"
#include "graphics/overlay_camera.h"
#include "graphics/sprite_batch.h"

namespace coney::hud {

/// The level's map on the radar, from its level record (`CfgLevelName`): the sprite sheet named after the world holds
/// the map's texture, and three floats map world metres onto it (docs/research/hud.md#the-radar-on-screen).
struct RadarMap {
    std::string sheet;    ///< The world name (record `+0x39`); the sheet is the resource named by its CRC-32.
    float offsetX = 0.0F; ///< Record `+0x6c` (argument 13): added to the world x.
    float offsetY = 0.0F; ///< Record `+0x70` (argument 14): the world y is taken from it.
    float scale = 0.0F;   ///< Record `+0x74` (argument 15): world metres across the texture's width.

    /// Whether the map can be drawn: a sheet named and a scale above 0.
    [[nodiscard]] bool usable() const { return !sheet.empty() && scale > 0.0F; }
};

/// One radar blip a script or the code asked for, by the object's handle (a slot of `0x40` bytes, layer 0).
struct RadarBlip {
    int type = 10; ///< Slot `+0x2c`: 10 mission objective, 1 secondary, 2-4 dealers, 6-9 humans.
    int icon = 69; ///< Slot `+0x18`: a `part_page0` rectangle; 69 the plain dot.
    /// The dot's size (`+0xc0`): 0.7 at the start, multiplied by each icon set's factor (they compound).
    float scale = 0.7F;
    graphics::Rgba colour = graphics::kWhite; ///< Slot `+0x14`. **Coney stand-in**: white where the page gives none.
    bool flashing = false;                    ///< `HUDSetRadarObjectFlash` (the dot's blinking state).
    bool iconLock = false;                    ///< Slot `+0x34`: icon and scale changes are ignored while set.
    int flashCountdown = 0;                   ///< Slot `+0x32`: a new objective blinks for 100 updates.
};

/// What the radar of player 0 is given each step: where the player stands and faces, how fast he moves and where the
/// camera looks. Headings are radians in the humans' convention: 0 faces +y, positive turns to the left.
struct RadarView {
    bool known = false;         ///< False until a mode gives a view: no map or blips are placed without one.
    anim::Vec3 position{};      ///< The player's feet, game axes (radar `+0x70`).
    float heading = 0.0F;       ///< The player's facing.
    float cameraHeading = 0.0F; ///< The active camera's facing (from the matrix at radar `+0x80`).
    float speed = 0.0F;         ///< The player's speed, m/s.
};

/// The radar disc's colour state (radar `+0x38`): grey, or blue while the player stands on shadow ground and may hide
/// (docs/research/stealth.md#hud-cue). The third state (100, 50, 50, 140) has no caller.
enum class RadarTint : std::uint8_t { Grey = 0, Blue = 1 };

/// The radars' state: on or off per player (each radar's `+0x04`), whether they come back on their own after a
/// letterbox or fade (`+0x177ac`, the scripts' last radar call), the blips, the level's map and the zoom, and each
/// disc's tint with the one it blends from and when it changed (`+0x38`, `+0x3c`, `+0x40`).
struct RadarState {
    std::array<bool, 2> on{true, true};
    bool scriptOn = true; ///< `+0x177ac`: set by the level's set-up and `HUDTurnOnRadar`, cleared by `HUDTurnOffRadar`.
    std::map<double, RadarBlip> blips;
    RadarMap map;
    /// The radius shown at rest and at full speed (`+0x28`, `+0x24`, `HUDRadarSetRange`). **Coney stand-in**: 50 and
    /// 75 m, `level99`'s values at run time, until a script sets them (who sets them first is not traced).
    float rest = 50.0F;
    float fast = 75.0F;
    float zoom = 50.0F;     ///< `+0x20`: the radius shown now, metres.
    float zoomScale = 1.0F; ///< `+0x2920` (`HUDSetRadarZoomScale`).
    RadarView view;
    std::uint64_t lastMs = 0;  ///< The game time of the last radar step (the zoom's easing).
    std::uint64_t updates = 0; ///< Radar steps taken (the blips' blinking).
    /// The chase HUD (the pursuit widget at `0x00609e80`) exists. **Coney stand-in**: it is not built or drawn; only
    /// `HUDSetChaseHUDState_DESTROY` clears it.
    bool chaseHud = false;
    std::array<RadarTint, 2> tint{};
    std::array<RadarTint, 2> previousTint{};
    std::array<std::uint64_t, 2> tintChangedMs{};
};

/// The full speed of the zoom's easing, m/s, and its rate per millisecond.
inline constexpr float kRadarFullSpeed = 12.0F;
inline constexpr float kRadarZoomRate = 0.0004F;
/// A blip's offset from the disc's centre per (distance / zoom), in overlay units at depth 1, and the edge's share of
/// it for a blip beyond the zoom (`0x0050e9bc`).
inline constexpr float kRadarBlipReach = 0.12F;
inline constexpr float kRadarEdge = 0.9F;
/// The disc's size: R = 0.9 × 0.19 × w / 2, w the third value of the active camera's slot `+0xc4` (about 1.33 as
/// measured at run time; inferred: the overlay camera's 4:3 width), stretched by the default video mode's factors
/// (`0x01` set, `0x20` clear, 4:3).
inline constexpr float kRadarViewWidth = 1.33F;
inline constexpr float kRadarRadius = 0.9F * 0.19F * kRadarViewWidth / 2.0F;
inline constexpr float kRadarStretchX = 1.1F;
inline constexpr float kRadarStretchY = 1.0F;
/// The disc's segments, and the share of R the filled part covers (`0x0050e9dc`).
inline constexpr int kRadarSegments = 32;
inline constexpr float kRadarFilled = 0.825F;
/// The disc's colour in state 0 (`0x0050e9e8`): grey, alpha 240.
inline constexpr graphics::Rgba kRadarDiscColour{191, 191, 191, 240};
/// Its colour in state 1: blue, alpha 240.
inline constexpr graphics::Rgba kRadarBlueColour{100, 120, 200, 240};
/// How long a change of the disc's state blends, ms.
inline constexpr std::uint64_t kRadarTintBlendMs = 500;

/// The disc's colour for `tint`: kRadarDiscColour or kRadarBlueColour (`0x0050e9e8`).
[[nodiscard]] graphics::Rgba radarTintColour(RadarTint tint);
/// A dot's start size (`0x003e5bf8`), the factor the human and dealer blips' icons are set at, and a dot's height
/// factor in the particle draw (`0x0039b020`).
inline constexpr float kRadarDotSize = 0.7F;
inline constexpr float kRadarIconFactor = 0.8F;
inline constexpr float kRadarDotHeight = 0.925F;
/// The tint of the dealers' icons 29-31 (`0x63db4bff`), whoever sets them (`HUD_RadarSetBlipIcon`, `0x001b2ca0`).
inline constexpr graphics::Rgba kRadarDealerColour{0x63, 0xdb, 0x4b, 0xff};
/// A new objective blip's blinking: 100 updates, its alpha toggled every 4.
inline constexpr int kRadarObjectiveFlash = 100;
inline constexpr int kRadarFlashPeriod = 4;

/// The zoom one step later: `zoom` eased toward (rest + (fast − rest) × min(speed, 12) / 12) × zoomScale by
/// min(0.0004 × ms, 1) of the difference.
/// @orig 0x001c60b0 Radar_Render (RadarHUD.cpp)
[[nodiscard]] float radarZoomStep(const RadarState& radar, float speed, std::uint32_t ms);

/// An offset on the disc in overlay units at depth 1: x to the right, y up.
struct RadarOffset {
    float x = 0.0F;
    float y = 0.0F;
};

/// Where a blip at world position `at` sits on the disc, from its centre: the offset from the player turned so that
/// the camera's view points up, × 0.12 / zoom inside the zoom, otherwise on the edge at 0.9 × 0.12 along its direction.
/// @orig 0x001c5210 Radar_Update (RadarHUD.cpp)
[[nodiscard]] RadarOffset radarBlipOffset(const RadarView& view, float zoom, anim::Vec3 at);

/// The map texture's coordinates under a disc point (`px`, `py`) given in units of the disc's radius (x right, y up):
/// the point's world position, the disc's radius being `zoom` metres, turned by the camera, through the level's map:
/// u = (x + offsetX) / scale, v = (offsetY − y) / scale.
/// @orig 0x001c60b0 Radar_Render (RadarHUD.cpp)
[[nodiscard]] std::array<float, 2> radarMapUv(const RadarMap& map, const RadarView& view, float zoom, float px,
                                              float py);

/// A dot's size in overlay units for icon rectangle `uv` of a `texWidth` × `texHeight` sheet at `size`: 2 × size ×
/// (u1 − u0) across, 2 × size × (v1 − v0) × texHeight / texWidth × 0.925 down.
/// @orig 0x0039b020 ParticleTask_Draw (unknown)
[[nodiscard]] std::array<float, 2> radarDotSize(const graphics::UvRect& uv, float texWidth, float texHeight,
                                                float size);

/// Adds the disc of the map to `batch` (the map sheet's batch) as triangles: a filled disc to 0.825 R in `colour`, then
/// a ring to R whose alpha falls from the colour's to 0, 32 segments starting at the top, centred at `centre` with
/// radii `rx` and `ry` (overlay units at the centre's depth).
/// @orig 0x001c60b0 Radar_Render (RadarHUD.cpp)
/// @orig 0x0017bc28 Im2D_DrawTexturedDisc (unknown)
/// @orig 0x0017b8a8 Im2D_DrawTexturedRing (unknown)
void addRadarDisc(graphics::SpriteBatch& batch, graphics::OverlayPoint centre, float rx, float ry,
                  const RadarState& radar, graphics::Rgba colour);

/// Whether a blip of this kind shows now: never type 5 (no blip); types 6 and 8 (enemies, police) only when marked by
/// the scanner (**Coney stand-in**: no marks yet, so never); a new objective blinks for its first 100 updates; a
/// flashing blip blinks the same way.
[[nodiscard]] bool radarBlipShown(const RadarBlip& blip, std::uint64_t updates);

} // namespace coney::hud
