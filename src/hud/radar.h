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
/// the map's texture, and three floats map world metres onto it (docs/research/hud-radar.md#the-radar-on-screen).
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
    /// Slot `+0x00` with in-use `+0x31` = 2: a fixed point the blip stays at instead of following its object. Nothing
    /// in the original makes such a slot (docs/research/gui.md#radar-update).
    std::optional<anim::Vec3> fixedPlace{};
    /// Radar_AddBlip's batch and first show or hide have been applied (settleRadarBlip(), at the next radar step).
    bool settled = false;
    int depth = 0;         ///< The depth of the sprite batch the dot draws in (slot `+0x20`); higher draws on top.
    bool marked = false;   ///< Slot `+0x30`: always for types 7, 10 and 12, else set by the scanner's mark.
    int markTimer = 0;     ///< Slot `+0x2e`: updates left of the scanner's mark (300 a mark).
    bool dotShown = false; ///< The dot's own shown state (its hide flag, messages `0x2a` and `0x29`).
    bool hasDot = true;    ///< Slot `+0x24` names a dot: every blip but one added as type 9 (the arrow widget).
    std::optional<anim::Vec3> place{}; ///< Where the last radar step found it.
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
    /// Radar 0's shown (`+0x08`, Radar_Show) and hidden (`+0x0c`, Radar_HideAllBlips) flags: its dots are set by
    /// the blip step only while it is shown and not hidden. Coney's blips are radar 0's.
    bool shown = true;
    bool hidden = false;
    /// `+0x14`: enemy dots may show beyond the zoom. 0 from `HUD_Construct` and `Radar_Setup`, and no other writer
    /// was found (confirmed (runtime): 0).
    bool edgeEnemies = false;
    /// `+0x18`: every dot inside the zoom shows and the scanner's marks never run out. 1 from `HUD_Construct`, and no
    /// other writer was found (confirmed (runtime): 1).
    bool keepShown = true;
    /// The player arrow widget's icon (the type-9 slot's sprite word): 362 as `Radar_AddBlip` makes it.
    int playerIcon = 362;
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

/// Whether a shown dot (RadarBlip::dotShown) is lit this update: it blinks, its alpha toggled every 4 updates, for a
/// new objective's first 100 updates and while flashing.
/// @orig 0x001c5210 Radar_Update (RadarHUD.cpp)
[[nodiscard]] bool radarBlipShown(const RadarBlip& blip, std::uint64_t updates);

/// The scanner's mark lasts this many radar updates (`Radar_MarkBlip`).
inline constexpr int kRadarMarkUpdates = 300;
/// An objective more than 1 m (`0x0050e9ac`) above or below the player and within 20 m (`0x0050e9b8`, |dx| + |dy|)
/// shows an arrow; the arrow's size factors (`0x0050e9b0`, `0x0050e9b4`).
inline constexpr float kRadarHeightStep = 1.0F;
inline constexpr float kRadarHeightReach = 20.0F;
inline constexpr float kRadarArrowFactor = 0.9F;
inline constexpr float kRadarTargetArrowFactor = 1.2F;
/// The objective icons and the arrows that stand for them above (75) and below (74).
inline constexpr int kRadarRedIcon = 27;
inline constexpr int kRadarYellowIcon = 28;
inline constexpr int kRadarTargetIcon = 22;
inline constexpr int kRadarAboveIcon = 75;
inline constexpr int kRadarBelowIcon = 74;
/// The arrow's colour, which remembers the icon it stands for: red for 27, yellow for 28, purple for 22.
inline constexpr graphics::Rgba kRadarRaisedRed{0xb0, 0x00, 0x00, 0xff};
inline constexpr graphics::Rgba kRadarRaisedYellow{0xe3, 0xbc, 0x42, 0xff};
inline constexpr graphics::Rgba kRadarRaisedPurple{0x6a, 0x41, 0x83, 0xff};
/// A type-12 blip's colour, set every update.
inline constexpr graphics::Rgba kRadarType12Colour{0x80, 0x80, 0x80, 0xf0};
/// Icons 25 and 26 draw in the radar's top batch (`+0x48`, depth 10,000).
inline constexpr int kRadarTopBatchDepth = 10000;
/// The police hint: tutorial string 8, for a police dot within 0.9 × the zoom.
inline constexpr std::uint32_t kRadarPoliceHint = 8;
inline constexpr float kRadarPoliceHintReach = 0.9F;
/// The levels that never give it (`level80`, `level99`).
inline constexpr std::array<int, 2> kRadarNoPoliceHintLevels{80, 99};

/// The depth of the batch `Radar_AddBlip` puts a dot of `type` in (`Radar_Setup`'s batches): 9,000 for objectives,
/// 7,000 type 2, 6,000 type 3, 1,000 types 6-8 (layer 0) and 5,000 the rest.
/// @orig 0x001c4d00 Radar_AddBlip (RadarHUD.cpp)
[[nodiscard]] int radarBatchDepth(int type);

/// `Radar_AddBlip`'s batch and first show: types 7, 10 and 12 are marked for good, and the dot shows only when marked
/// while the radar is shown and not hidden; a type-9 slot is the player's arrow widget, unmarked and with no dot.
/// @orig 0x001c4d00 Radar_AddBlip (RadarHUD.cpp)
void settleRadarBlip(RadarBlip& blip, const RadarState& radar);

/// `Radar_SetSlotIcon`: unless the icon is locked, the icon and the size × `factor`.
/// @orig 0x001c4690 Radar_SetSlotIcon (RadarHUD.cpp)
void setRadarSlotIcon(RadarBlip& blip, float factor, int icon);

/// `Radar_MarkBlip`: the enemy scanner saw this blip's human; it is marked for 300 updates.
/// @orig 0x001c4be0 Radar_MarkBlip (RadarHUD.cpp)
void markRadarBlip(RadarBlip& blip);

/// `Radar_Show` on radar 0: while it is on, every dot shown; then shown set and hidden cleared.
/// @orig 0x001c4388 Radar_Show (RadarHUD.cpp)
void showRadarDots(RadarState& radar);
/// `Radar_Hide` on radar 0: while it is on (`on`, its on flag as the hide comes; RadarState::on may already be
/// clear), every dot hidden and shown cleared.
/// @orig 0x001c4448 Radar_Hide (RadarHUD.cpp)
void hideRadarDots(RadarState& radar, bool on);
/// `Radar_HideAllBlips` on radar 0: hidden set and every dot hidden.
/// @orig 0x001c42e8 Radar_HideAllBlips (RadarHUD.cpp)
void hideAllRadarDots(RadarState& radar);

/// What the blip step needs besides the radar: the objects' places, a human's brain type, and whether the police
/// hint may be given (the game's hints on, not an Armies of the Night level, not yet given, not levels 80 and 99).
struct RadarStepContext {
    std::function<std::optional<anim::Vec3>(double handle)> locate{};
    std::function<std::optional<int>(double handle)> brainType{}; ///< Human_GetBrain `+0x04`; none for a non-human.
    bool policeHintAllowed = false;
};

/// `Radar_Update`'s blip loop for radar 0 (call only while it is on): each blip followed, its objective arrow, type-12
/// colour, icons 25 and 26's batch, its dot shown or hidden by its kind and place, and the scanner's mark counted down.
/// Returns true when the police hint 8 is due (the caller queues it and sets its once-flag).
/// @orig 0x001c5210 Radar_Update (RadarHUD.cpp)
/// @orig 0x001c6730 Radar_MoveSlotToBatch48 (RadarHUD.cpp)
bool stepRadarBlips(RadarState& radar, const RadarStepContext& context);

/// The player arrow widget's size and whether it turns with the player, for its icon.
struct RadarArrowLook {
    float size = 0.03F; ///< Its width in overlay units.
    bool turned = true; ///< Turned by the player's heading minus the camera's.
};
/// 0.035 and upright for icons 25 and 26; upright for the two-player icons 0x15e, 0x15f, 0x168 and 0x169; else 0.03.
/// @orig 0x001c5210 Radar_Update (RadarHUD.cpp)
[[nodiscard]] RadarArrowLook radarPlayerArrowLook(int icon);

} // namespace coney::hud
