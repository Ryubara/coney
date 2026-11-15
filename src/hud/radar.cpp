// SPDX-License-Identifier: GPL-3.0-or-later
// The radar's map disc, the placement of its blips and Radar_Update's blip steps
// (docs/research/hud-radar.md#the-radar-on-screen, docs/research/gui.md#fn-radarhud,
// docs/research/gui.md#radar-update).
#include "hud/radar.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace coney::hud {

namespace {

// The camera's right and forward in the game's plan (headings: 0 faces +y, positive to the left).
struct PlanAxes {
    float rightX, rightY, forwardX, forwardY;
};

PlanAxes axesOf(float heading) {
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    return PlanAxes{c, s, -s, c};
}

// `colour` with alpha `alpha`.
graphics::Rgba withAlpha(graphics::Rgba colour, std::uint8_t alpha) {
    colour.a = alpha;
    return colour;
}

// Types 5, 6 and 8: the dots of humans against the player (none is made for type 5), shown by the scanner's marks.
// @orig 0x001c5210 Radar_Update (RadarHUD.cpp)
bool isEnemyType(int type) { return type == 5 || type == 6 || type == 8; }

// Icons 25 and 26 draw in the radar's top batch.
// @orig 0x001c5210 Radar_Update (RadarHUD.cpp)
bool isTopBatchIcon(int icon) { return icon == 25 || icon == 26; }

// Whether the radar's dots are shown now (`+0x08` set, `+0x0c` clear).
// @orig 0x001c5210 Radar_Update (RadarHUD.cpp)
bool dotsShowing(const RadarState& radar) { return radar.shown && !radar.hidden; }

// Settles every blip added since the last step, so a show or hide finds it made as Radar_AddBlip would have.
// @orig 0x001c4d00 Radar_AddBlip (RadarHUD.cpp)
void settleNewBlips(RadarState& radar) {
    for (auto& [handle, blip] : radar.blips) {
        if (!blip.settled) {
            settleRadarBlip(blip, radar);
        }
    }
}

// The objective's arrow: an objective over 1 m above or below the player and within 20 m (|dx| + |dy|) shows arrow
// 75 or 74 in the colour of the icon it stands for, which brings that icon back once it is level or far again.
// @orig 0x001c5210 Radar_Update (RadarHUD.cpp)
void stepObjectiveHeight(RadarBlip& blip, anim::Vec3 player, anim::Vec3 at) {
    const bool near = std::abs(at.x - player.x) + std::abs(at.y - player.y) < kRadarHeightReach;
    int arrow = 0;
    if (near && at.z > player.z + kRadarHeightStep) {
        arrow = kRadarAboveIcon;
    } else if (near && at.z < player.z - kRadarHeightStep) {
        arrow = kRadarBelowIcon;
    }
    if (arrow != 0) {
        // Not ported (#332): the first such objective in a story level with the game's hints on queues hint 22 once.
        if (blip.icon == arrow) {
            return;
        }
        if (blip.icon == kRadarRedIcon) {
            blip.colour = kRadarRaisedRed;
            setRadarSlotIcon(blip, kRadarArrowFactor, arrow);
        } else if (blip.icon == kRadarYellowIcon) {
            blip.colour = kRadarRaisedYellow;
            setRadarSlotIcon(blip, kRadarArrowFactor, arrow);
        } else if (blip.icon == kRadarTargetIcon) {
            blip.colour = kRadarRaisedPurple;
            setRadarSlotIcon(blip, kRadarTargetArrowFactor, arrow);
        }
        return;
    }
    // Level, or far: an icon of its own stays; an arrow (or any other icon) gives back the icon its colour names, at
    // its old size, and every such objective is white.
    if (blip.icon == kRadarRedIcon || blip.icon == kRadarYellowIcon || blip.icon == kRadarTargetIcon) {
        return;
    }
    if (blip.colour == kRadarRaisedRed) {
        setRadarSlotIcon(blip, 1.0F / kRadarArrowFactor, kRadarRedIcon);
    } else if (blip.colour == kRadarRaisedYellow) {
        setRadarSlotIcon(blip, 1.0F / kRadarArrowFactor, kRadarYellowIcon);
    } else if (blip.colour == kRadarRaisedPurple) {
        setRadarSlotIcon(blip, 1.0F / kRadarTargetArrowFactor, kRadarTargetIcon);
    }
    blip.colour = graphics::kWhite;
}

} // namespace

float radarZoomStep(const RadarState& radar, float speed, std::uint32_t ms) {
    const float fraction = std::min(std::max(speed, 0.0F), kRadarFullSpeed) / kRadarFullSpeed;
    const float target = (radar.rest + ((radar.fast - radar.rest) * fraction)) * radar.zoomScale;
    const float k = std::min(kRadarZoomRate * static_cast<float>(ms), 1.0F);
    return radar.zoom + ((target - radar.zoom) * k);
}

RadarOffset radarBlipOffset(const RadarView& view, float zoom, anim::Vec3 at) {
    // The offset in the camera's frame: across to the right, along the view up.
    const PlanAxes axes = axesOf(view.cameraHeading);
    const float dx = at.x - view.position.x;
    const float dy = at.y - view.position.y;
    const float across = (dx * axes.rightX) + (dy * axes.rightY);
    const float along = (dx * axes.forwardX) + (dy * axes.forwardY);
    const float distance = std::hypot(across, along);
    if (zoom <= 0.0F) {
        return {};
    }
    if (distance < zoom) {
        return RadarOffset{across * kRadarBlipReach / zoom, along * kRadarBlipReach / zoom};
    }
    // Beyond the zoom: on the edge along its direction.
    const float edge = kRadarEdge * kRadarBlipReach / distance;
    return RadarOffset{across * edge, along * edge};
}

std::array<float, 2> radarMapUv(const RadarMap& map, const RadarView& view, float zoom, float px, float py) {
    // The disc point's world position: `zoom` metres per radius, across and along the camera's view.
    const PlanAxes axes = axesOf(view.cameraHeading);
    const float x = view.position.x + (zoom * ((px * axes.rightX) + (py * axes.forwardX)));
    const float y = view.position.y + (zoom * ((px * axes.rightY) + (py * axes.forwardY)));
    if (map.scale <= 0.0F) {
        return {0.0F, 0.0F};
    }
    return {(x + map.offsetX) / map.scale, (map.offsetY - y) / map.scale};
}

std::array<float, 2> radarDotSize(const graphics::UvRect& uv, float texWidth, float texHeight, float size) {
    if (texWidth <= 0.0F) {
        return {0.0F, 0.0F};
    }
    return {2.0F * size * std::abs(uv.u1 - uv.u0),
            2.0F * size * std::abs(uv.v1 - uv.v0) * (texHeight / texWidth) * kRadarDotHeight};
}

void addRadarDisc(graphics::SpriteBatch& batch, graphics::OverlayPoint centre, float rx, float ry,
                  const RadarState& radar, graphics::Rgba colour) {
    // A corner of the disc at `share` of R in direction `angle` (0 to the right, counter-clockwise), its texture
    // coordinates from the map under it.
    const auto corner = [&](float share, float angle, std::uint8_t alpha) {
        const float px = share * std::cos(angle);
        const float py = share * std::sin(angle);
        const std::array<float, 2> uv = radarMapUv(radar.map, radar.view, radar.zoom, px, py);
        return graphics::OverlayVertex{graphics::OverlayPoint{centre.x + (px * rx), centre.y + (py * ry), centre.z},
                                       uv[0], uv[1], withAlpha(colour, alpha)};
    };
    const graphics::OverlayVertex middle = corner(0.0F, 0.0F, colour.a);
    const float step = 2.0F * std::numbers::pi_v<float> / static_cast<float>(kRadarSegments);
    // The segments start at the top (π/2) and go round.
    for (int i = 0; i < kRadarSegments; ++i) {
        const float a0 = (std::numbers::pi_v<float> / 2.0F) + (step * static_cast<float>(i));
        const float a1 = a0 + step;
        const graphics::OverlayVertex inner0 = corner(kRadarFilled, a0, colour.a);
        const graphics::OverlayVertex inner1 = corner(kRadarFilled, a1, colour.a);
        const graphics::OverlayVertex outer0 = corner(1.0F, a0, 0);
        const graphics::OverlayVertex outer1 = corner(1.0F, a1, 0);
        // The filled part, then the soft edge's two triangles.
        batch.addTriangle(middle, inner0, inner1);
        batch.addTriangle(inner0, outer0, outer1);
        batch.addTriangle(inner0, outer1, inner1);
    }
}

bool radarBlipShown(const RadarBlip& blip, std::uint64_t updates) {
    if (blip.flashCountdown > 0 || blip.flashing) {
        return (updates / kRadarFlashPeriod) % 2 == 0;
    }
    return true;
}

int radarBatchDepth(int type) {
    switch (type) {
    case 10:
        return 9000;
    case 6:
    case 7:
    case 8:
        // Layer 0's batch (`+0x5c`); layer 1's (`+0x60`, 2,000) holds the second dots Coney does not make.
        return 1000;
    case 2:
        return 7000;
    case 3:
        return 6000;
    default:
        return 5000;
    }
}

void settleRadarBlip(RadarBlip& blip, const RadarState& radar) {
    blip.settled = true;
    blip.depth = radarBatchDepth(blip.type);
    blip.marked = blip.type == 7 || blip.type == 9 || blip.type == 10 || blip.type == 12;
    blip.dotShown = blip.marked && dotsShowing(radar);
}

void setRadarSlotIcon(RadarBlip& blip, float factor, int icon) {
    if (blip.iconLock) {
        return;
    }
    blip.icon = icon;
    blip.scale *= factor;
}

void markRadarBlip(RadarBlip& blip) {
    // The first mark re-sends the slot's colour to the dot; Coney's dot draws in the slot's colour already.
    blip.markTimer = kRadarMarkUpdates;
    blip.marked = true;
}

void showRadarDots(RadarState& radar) {
    if (!radar.on.at(0)) {
        return;
    }
    settleNewBlips(radar);
    for (auto& [handle, blip] : radar.blips) {
        blip.dotShown = true;
    }
    radar.hidden = false;
    radar.shown = true;
}

void hideRadarDots(RadarState& radar, bool on) {
    if (!on) {
        return;
    }
    settleNewBlips(radar);
    for (auto& [handle, blip] : radar.blips) {
        blip.dotShown = false;
    }
    radar.shown = false;
}

void hideAllRadarDots(RadarState& radar) {
    radar.hidden = true;
    settleNewBlips(radar);
    for (auto& [handle, blip] : radar.blips) {
        blip.dotShown = false;
    }
}

bool stepRadarBlips(RadarState& radar, const RadarStepContext& context) {
    const bool showing = dotsShowing(radar);
    bool policeHint = false;
    for (auto& [handle, blip] : radar.blips) {
        if (!blip.settled) {
            settleRadarBlip(blip, radar);
        }
        // The player's own blip is the arrow widget, which Coney draws apart (Hud::renderPlayerArrow).
        if (blip.type == 9) {
            continue;
        }
        // 1. Icons 25 and 26 go to the top batch (Radar_MoveSlotToBatch48): a new dot, at the same size and colour,
        // hidden unless the radar shows and the blip is marked.
        if (isTopBatchIcon(blip.icon) && blip.depth != kRadarTopBatchDepth) {
            blip.depth = kRadarTopBatchDepth;
            blip.dotShown = showing && blip.marked;
        }
        // 2. Where it is: its fixed point, or its object, whose objective arrow follows the height.
        std::optional<anim::Vec3> at = blip.fixedPlace;
        if (!at && context.locate) {
            at = context.locate(handle);
            if (at && blip.type == 10) {
                stepObjectiveHeight(blip, radar.view.position, *at);
            }
        }
        // **Coney's choice** (#1018): a blip whose object cannot be found keeps its slot and skips the step, where
        // the original frees the slot of a dead handle (Radar_FreeSlot): Coney cannot yet locate every kind of object.
        if (!at) {
            continue;
        }
        blip.place = at;
        // 3. A new objective's blinking counts down.
        if (blip.flashCountdown > 0) {
            --blip.flashCountdown;
        }
        // 4. Its dot by its kind and whether it is inside the zoom.
        const float distance = std::hypot(at->x - radar.view.position.x, at->y - radar.view.position.y);
        const bool inside = distance < radar.zoom;
        if (blip.type == 12) {
            blip.colour = kRadarType12Colour;
        } else if (isEnemyType(blip.type)) {
            // A police officer (brain type 1) close in queues the police hint once.
            if (inside && blip.type == 8 && context.policeHintAllowed && !policeHint &&
                distance < kRadarPoliceHintReach * radar.zoom && showing && (blip.marked || radar.keepShown) &&
                context.brainType && context.brainType(handle) == 1) {
                policeHint = true;
            }
            // Enemies show only while marked, and beyond the zoom only with `+0x14`.
            blip.dotShown = showing && blip.marked && (inside || radar.edgeEnemies);
        }
        // With `+0x18` every dot shows (beyond the zoom only with `+0x14` too).
        if (showing && radar.keepShown && (inside || radar.edgeEnemies)) {
            blip.dotShown = true;
        }
        // 5. The scanner's mark runs out: at 0 an enemy's mark ends and its dot goes, unless `+0x18` keeps them.
        if (blip.markTimer == 0) {
            if (isEnemyType(blip.type) && !radar.keepShown) {
                blip.marked = false;
                blip.dotShown = false;
            }
        } else {
            --blip.markTimer;
        }
    }
    return policeHint;
}

RadarArrowLook radarPlayerArrowLook(int icon) {
    if (isTopBatchIcon(icon)) {
        return RadarArrowLook{.size = 0.035F, .turned = false};
    }
    // The two-player arrows (#549) are upright too.
    if (icon == 0x15e || icon == 0x15f || icon == 0x168 || icon == 0x169) {
        return RadarArrowLook{.size = 0.03F, .turned = false};
    }
    return RadarArrowLook{};
}

} // namespace coney::hud
