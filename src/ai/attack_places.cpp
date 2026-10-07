// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_places.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>

#include "animation/anim_clip.h"

namespace coney::ai {

namespace {

// Clips that are alternatives (the longest counts).
SwingClips longest(std::initializer_list<std::uint32_t> ids) {
    SwingClips clips;
    for (const std::uint32_t id : ids) {
        clips.ids[clips.count++] = id;
    }
    return clips;
}

// Clips that play one after the other (their sum counts).
SwingClips chained(std::initializer_list<std::uint32_t> ids) {
    SwingClips clips = longest(ids);
    clips.sum = true;
    return clips;
}

// The strikes' anims by kind 0-9 (X1, S1, XX2, SX2, XS2, SS2, SSX3, SSS3 and its two holds).
constexpr std::array<std::uint32_t, 10> kStrikeAnims{11, 12, 13, 15, 14, 16, 17, 19, 18, 20};

// One clip's playing time in `anims`, ms; none when the set lacks it.
std::optional<int> playingMs(const characters::AnimSet& anims, std::uint32_t id) {
    const anim::AnimClip* clip = anims.clip(id);
    const float rate = anims.rate(id);
    if (clip == nullptr || rate <= 0.0F) {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(clip->duration / rate * 1000.0F));
}

} // namespace

SwingClips swingClipsOf(int kind, bool targetDown) {
    if (kind == 0 && targetDown) {
        return longest({194});
    }
    if (kind == 1 && targetDown) {
        return {};
    }
    if (kind >= 0 && kind <= 9) {
        return longest({kStrikeAnims[static_cast<std::size_t>(kind)]});
    }
    switch (kind) {
    case 10:
        return longest({25});
    case 11:
        return longest({21});
    case 12:
        return longest({193});
    case 13:
        return longest({194});
    case 14:
        return longest({237});
    case 15:
        return longest({664});
    case 16:
        return longest({653, 655});
    case 17:
        return longest({657, 659});
    case 19:
        return longest({0});
    case 20:
        return longest({1});
    case 21:
        return chained({3, 5});
    case 22:
        return chained({70, 72});
    case 23:
        return chained({466, 467});
    case 25:
        return longest({147, 149, 151, 153});
    case 26:
        return longest({57});
    case 27:
        return longest({59});
    case 28:
        return longest({61});
    case 29:
        return longest({155, 157, 159, 161});
    case 30:
        return longest({118});
    case 31:
        return longest({96, 108});
    case 32:
    case 33:
    case 34:
        return longest({100, 112});
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
        return longest({225});
    case 40:
        return longest({248});
    case 41:
        return longest({252});
    case 43:
        return longest({246});
    case 44:
        return longest({242});
    default:
        return {};
    }
}

int swingTimeMs(int kind, bool targetDown, const characters::AnimSet& anims) {
    const SwingClips clips = swingClipsOf(kind, targetDown);
    int total = 0;
    bool found = false;
    for (std::size_t i = 0; i < clips.count; ++i) {
        if (const std::optional<int> ms = playingMs(anims, clips.ids[i]); ms.has_value()) {
            found = true;
            total = clips.sum ? total + *ms : std::max(total, *ms);
        }
    }
    return found ? total : kDefaultSwingMs;
}

int attackableGapMs(int swingMs, int spacing) {
    if (spacing == 0) {
        return swingMs;
    }
    return static_cast<int>(std::lround(static_cast<float>(swingMs) / static_cast<float>(spacing)));
}

void Spacing::raise(int gangStanding, int gangDown, bool targetIsWarrior) {
    if (!targetIsWarrior) {
        standing = std::max(standing, gangStanding);
    }
    down = std::max(down, gangDown);
}

bool ActivePlaces::holds(const void* attacker) const { return std::ranges::find(m_places, attacker) != m_places.end(); }

std::size_t ActivePlaces::held() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_places, [](const void* p) { return p != nullptr; }));
}

bool ActivePlaces::claim(const void* attacker, int spacing, bool attackable, bool targetsBack) {
    if (attacker == nullptr) {
        return false;
    }
    if (holds(attacker)) {
        return true;
    }
    const std::size_t used = std::min<std::size_t>(static_cast<std::size_t>(std::max(spacing, 0)), kActivePlaces);
    // A free place among the first `used`, when the target may be attacked or someone already swings at him.
    if (attackable || held() > 0) {
        for (std::size_t i = 0; i < used; ++i) {
            if (m_places[i] == nullptr) {
                m_places[i] = attacker;
                return true;
            }
        }
    }
    // His own target displaces the first holder once he may be attacked.
    if (targetsBack && attackable && used > 0) {
        m_places[0] = attacker;
        return true;
    }
    return false;
}

void ActivePlaces::release(const void* attacker) {
    for (const void*& place : m_places) {
        if (place == attacker) {
            place = nullptr;
        }
    }
}

} // namespace coney::ai
