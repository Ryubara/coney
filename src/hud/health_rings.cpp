// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/health_rings.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace coney::hud {

namespace {

// A fade's alpha after `ms` of it: ms × 0.51, at most 255.
std::uint8_t fadeAlpha(std::uint64_t ms) {
    return static_cast<std::uint8_t>(std::min(255.0F, static_cast<float>(ms) * kRingFadePerMs));
}

// An RGB colour with `alpha`.
std::array<std::uint8_t, 4> withAlpha(RingColour colour, std::uint8_t alpha) {
    return {colour.r, colour.g, colour.b, alpha};
}

} // namespace

RingColour healthColour(float percent) {
    if (percent > 75.0F) {
        return RingColour{76, 122, 27};
    }
    if (percent <= 0.1F) {
        return RingColour{16, 16, 16};
    }
    return RingColour{158, static_cast<std::uint8_t>(24 + static_cast<int>(percent * 1.1571F)), 24};
}

RingArc ringArcOf(int k, float value, float capacity) {
    const int segments = kRingSegments;
    const int rest = static_cast<int>(std::floor(static_cast<float>(segments) * (100.0F - capacity) / 100.0F));
    const int lost = static_cast<int>(std::floor(static_cast<float>(segments) * (100.0F - value) / 100.0F));
    if (rest > 0 && segments - k <= rest) {
        return RingArc::Rest;
    }
    if (segments - k <= lost) {
        return RingArc::Lost;
    }
    return RingArc::Fill;
}

std::vector<RingVertex> ringFan(const GroundRing& ring) {
    // The centre, then rim vertices 0-64 going round from the start angle.
    std::array<RingVertex, kRingSegments + 1> rim{};
    const float step = 2.0F * std::numbers::pi_v<float> / static_cast<float>(kRingSegments);
    for (int k = 0; k <= kRingSegments; ++k) {
        const float angle = ring.startAngle + static_cast<float>(k) * step;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        RingVertex& v = rim.at(static_cast<std::size_t>(k));
        v.position = anim::Vec3{ring.centre.x + ring.radius * c, ring.centre.y + ring.radius * s, ring.centre.z};
        v.du = kRingUvU * c;
        v.dv = -kRingUvV * s;
        const RingArc arc = ringArcOf(k, ring.value, ring.capacity);
        v.rgba = withAlpha(arc == RingArc::Rest ? ring.rest : arc == RingArc::Lost ? ring.lost : ring.fill, ring.alpha);
    }
    const RingVertex centre{ring.centre, 0.0F, 0.0F, withAlpha(ring.fill, ring.alpha)};
    // Flat shading: each triangle in its last vertex's colour.
    std::vector<RingVertex> out;
    out.reserve(static_cast<std::size_t>(kRingSegments) * 3);
    for (std::size_t k = 0; k < static_cast<std::size_t>(kRingSegments); ++k) {
        const std::array<std::uint8_t, 4> colour = rim.at(k + 1).rgba;
        RingVertex a = centre;
        RingVertex b = rim.at(k);
        a.rgba = colour;
        b.rgba = colour;
        out.push_back(a);
        out.push_back(b);
        out.push_back(rim.at(k + 1));
    }
    return out;
}

float HealthRings::pulseScale(std::uint64_t id) const {
    for (const auto& [who, pulse] : m_pulses) {
        if (who == id) {
            return std::min(1.0F + static_cast<float>(pulse.value) / 100.0F, kPulseMaxScale);
        }
    }
    return 1.0F;
}

std::uint8_t HealthRings::listAlpha(std::uint64_t id, std::uint64_t now) {
    const auto entry = std::ranges::find(m_list, id, &Shown::id);
    if (entry == m_list.end()) {
        return 0;
    }
    const std::uint64_t sinceTrigger = now - entry->lastTrigger;
    if (sinceTrigger >= kRingHoldMs + kRingFadeMs) {
        m_list.erase(entry);
        return 0;
    }
    // Fading in over 500 ms after first appearing, out over 500 ms from 4,000 ms after the last trigger.
    std::uint8_t alpha = fadeAlpha(now - entry->firstSeen);
    if (sinceTrigger > kRingHoldMs) {
        alpha = std::min(alpha, fadeAlpha(kRingFadeMs - (sinceTrigger - kRingHoldMs)));
    }
    return alpha;
}

float HealthRings::stepPulse(const RingHuman& human) {
    auto entry = std::ranges::find(m_pulses, human.id, &std::pair<std::uint64_t, Pulse>::first);
    if (entry == m_pulses.end()) {
        m_pulses.emplace_back(human.id, Pulse{});
        entry = std::prev(m_pulses.end());
    }
    Pulse& pulse = entry->second;
    // The value moves towards the target while the ring is drawn; on arriving the target goes back to 0.
    if (pulse.value < pulse.target) {
        pulse.value = std::min(pulse.target, pulse.value + kPulseStep);
    } else if (pulse.value > pulse.target) {
        pulse.value = std::max(pulse.target, pulse.value - kPulseStep);
    }
    if (pulse.value == pulse.target) {
        pulse.target = 0;
    }
    return std::min(1.0F + static_cast<float>(pulse.value) / 100.0F, kPulseMaxScale);
}

void HealthRings::queue(const RingHuman& human, const RingFrame& frame, std::uint8_t alpha, const PlayerState* state,
                        bool inner) {
    const float s = stepPulse(human);
    const float k = static_cast<float>(human.classByte) / 100.0F;
    const float lift = kRingHeight + kRingStackStep * static_cast<float>(m_queued++);
    GroundRing outer;
    outer.id = human.id;
    outer.lift = lift;
    outer.centre = anim::Vec3{human.feet.x, human.feet.y, human.feet.z + lift};
    outer.startAngle = frame.cameraHeading + kRingStartOffset;
    outer.radius = kOuterRingRadius * s;
    outer.capacity = std::clamp(100.0F * k, 0.0F, 100.0F);
    const float health = std::clamp(human.healthPercent, 0.0F, 100.0F);
    outer.value = std::clamp(health * k, 0.0F, 100.0F);
    outer.fill = healthColour(health);
    outer.alpha = alpha;
    // A player's low health blinks: black for 232 ms, its colour for the next. **Coney's choice**: the game clock,
    // black first.
    if (human.player && health > 0.1F && health <= kRingBlinkPercent && (frame.nowMs / kRingBlinkMs) % 2 == 0) {
        outer.fill = kRingLost;
    }
    // Rage full: the whole ring gold for 200 ms, normal for 200 ms, three times; raging after that, a grey fill.
    const std::optional<std::uint64_t> flashStart = state != nullptr ? state->flashStart : std::nullopt;
    const std::uint64_t sinceFlash =
        flashStart.has_value() ? frame.nowMs - flashStart.value() : std::numeric_limits<std::uint64_t>::max();
    const bool flashing = sinceFlash < kRageFlashMs * 2 * static_cast<std::uint64_t>(kRageFlashes);
    if (flashing && (sinceFlash / kRageFlashMs) % 2 == 0) {
        outer.fill = kRingRageFlash;
        outer.lost = kRingRageFlash;
        outer.rest = kRingRageFlash;
        outer.alpha = 255;
    } else if (human.raging && human.player && !flashing) {
        outer.fill = kRingRaging;
    }
    m_rings.push_back(outer);
    if (!inner) {
        return;
    }
    GroundRing ring = outer;
    ring.radius = kInnerRingRadius * s;
    ring.fill = human.raging ? kRingRage : kRingPower;
    ring.lost = kRingLost;
    ring.rest = kRingRest;
    ring.alpha = alpha;
    const float value = human.raging ? static_cast<float>(human.rage) : std::clamp(human.powerPercent, 0.0F, 100.0F);
    ring.value = std::clamp(value * k, 0.0F, 100.0F);
    m_rings.push_back(ring);
}

void HealthRings::update(const RingFrame& frame) {
    m_rings.clear();
    m_markers.clear();
    m_queued = 0;
    // Nothing while the HUD is hidden: HideHud and a scene's letterbox take the rings away with the rest.
    if (!frame.hudShown) {
        return;
    }
    const std::uint64_t now = frame.nowMs;
    m_players.resize(frame.players.size());
    // A hit sets a human's pulse target to its damage, whether or not its rings are drawn.
    const auto noteHit = [this](const RingHuman& human) {
        if (human.damageTaken <= 0) {
            return;
        }
        auto entry = std::ranges::find(m_pulses, human.id, &std::pair<std::uint64_t, Pulse>::first);
        if (entry == m_pulses.end()) {
            m_pulses.emplace_back(human.id, Pulse{});
            entry = std::prev(m_pulses.end());
        }
        entry->second.target = std::clamp(human.damageTaken, 0, 100);
    };
    for (std::size_t i = 0; i < frame.players.size(); ++i) {
        const RingPlayer& player = frame.players[i];
        PlayerState& state = m_players[i];
        const RingHuman& human = player.human;
        noteHit(human);
        if (player.target) {
            noteHit(*player.target);
        }
        if (human.rageFull && !state.wasFull) {
            state.flashStart = now;
        }
        state.wasFull = human.rageFull;

        // A trigger (SELECT, a flash) sets the last trigger; a fight stance or low health renews it every update.
        const bool trigger =
            player.selectPressed || player.flashUsed || player.fightStance || human.healthPercent <= kRingStayPercent;
        if (trigger) {
            const auto entry = std::ranges::find(m_list, human.id, &Shown::id);
            if (entry != m_list.end()) {
                entry->lastTrigger = now;
            } else if (m_list.size() < kRingListSize) {
                m_list.push_back(Shown{human.id, now, now});
            }
        }
        const std::uint8_t listed = listAlpha(human.id, now);
        const std::uint8_t alpha = frame.forceAll ? std::uint8_t{255} : listed;
        if (human.canShow && alpha > 0) {
            queue(human, frame, alpha, &state, true);
        }

        // The player's target: fades in over 500 ms, gone at once when it stops being the target. A non-player's
        // outer ring only.
        if (!player.target || !player.target->canShow) {
            state.target = 0;
            continue;
        }
        const RingHuman& target = *player.target;
        if (state.target != target.id) {
            state.target = target.id;
            state.targetSince = now;
        }
        const std::uint8_t targetAlpha = fadeAlpha(now - state.targetSince);
        if (targetAlpha == 0) {
            continue;
        }
        queue(target, frame, targetAlpha, nullptr, target.player);
        if (player.holdingL1 && !target.player) {
            const float s = pulseScale(target.id);
            m_markers.push_back(
                TargetMarker{.id = target.id,
                             .centre = anim::Vec3{target.feet.x, target.feet.y, target.feet.z + kTargetMarkerHeight},
                             .lift = kTargetMarkerHeight,
                             .size = kTargetMarkerSize * s,
                             .alpha = targetAlpha,
                             .black = false});
        }
    }
}

} // namespace coney::hud
