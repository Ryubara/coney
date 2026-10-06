// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/tag_spots.h"

#include <algorithm>
#include <utility>

namespace coney::world_objects {

namespace {

// Message 0x19's states.
constexpr int kStateStop = 3;
constexpr int kStateBlank = 4;
constexpr int kStateFadeOut = 5;
constexpr int kStatePainted = 6;
constexpr int kStateFadeIn = 7;

} // namespace

TagSpot& TagSpots::spot(double tag) {
    const auto found = std::ranges::find(m_spots, tag, &TagSpot::handle);
    if (found != m_spots.end()) {
        return *found;
    }
    return m_spots.emplace_back(TagSpot{.handle = tag});
}

void TagSpots::tell(const TagSpot& spot, int message) {
    if (spot.tagger != 0 && m_send) {
        m_send(spot.tagger, message, spot.handle);
    }
}

void TagSpots::configure(double tag, std::uint32_t sprite, float start, float fade, float depth) {
    TagSpot& s = spot(tag);
    s.sprite = sprite;
    s.start = start;
    s.fadeStep = fade;
    s.depth = std::max(depth, 0.0F);
}

void TagSpots::setTagger(double tag, double human) {
    spot(tag).tagger = human;
    show(tag);
}

void TagSpots::show(double tag) {
    TagSpot& s = spot(tag);
    if (m_flag) {
        m_flag(tag, true);
    }
    const bool work = (s.sprayMode == TagSpot::kPaintIn && s.fraction < 1.0F) ||
                      (s.sprayMode == TagSpot::kWipeOut && s.fraction > 0.0F);
    if (work && s.tagger != 0) {
        tell(s, kTaggerSpray);
        setState(tag, s.sprayMode);
    } else {
        tell(s, kTaggerDone);
    }
}

void TagSpots::hide(double tag) {
    TagSpot& s = spot(tag);
    if (m_flag) {
        m_flag(tag, false);
    }
    tell(s, kTaggerDone);
    s.fadeMode = TagSpot::kStill;
}

void TagSpots::remove(double tag) {
    std::erase_if(m_spots, [tag](const TagSpot& s) { return s.handle == tag; });
}

void TagSpots::setState(double tag, int state) {
    TagSpot& s = spot(tag);
    switch (state) {
    case kStateStop:
        s.fadeMode = TagSpot::kStill;
        tell(s, kTaggerDone);
        break;
    case kStateBlank:
        s.fraction = 0.0F;
        s.sprayMode = TagSpot::kPaintIn;
        break;
    case kStateFadeOut:
        s.fadeMode = TagSpot::kFadingOut;
        break;
    case kStatePainted:
        s.fraction = 1.0F;
        break;
    case kStateFadeIn:
        s.fadeMode = TagSpot::kFadingIn;
        break;
    default:
        break;
    }
}

void TagSpots::setSprayMode(double tag, bool paintIn) {
    spot(tag).sprayMode = paintIn ? TagSpot::kPaintIn : TagSpot::kWipeOut;
}

void TagSpots::setFraction(double tag, float fraction) { spot(tag).fraction = fraction; }

void TagSpots::endSpray(double tag, bool finished) {
    if (finished) {
        setFraction(tag, 1.0F);
        setState(tag, kStateStop);
    }
    hide(tag);
}

bool TagSpots::spray(double tag, double human, float fraction, bool finished) {
    TagSpot& s = spot(tag);
    s.tagger = human;
    s.fraction = fraction;
    if (finished) {
        endSpray(tag, true);
    }
    const TagSpot* after = find(tag);
    return finished && after != nullptr && after->fraction >= 1.0F;
}

void TagSpots::update() {
    // Index by position: a state 3 may tell a tagger, whose handler must not change the list under us.
    for (std::size_t i = 0; i < m_spots.size(); ++i) {
        TagSpot& s = m_spots[i];
        if (s.fadeMode == TagSpot::kFadingIn) {
            s.fraction = std::min(1.0F, s.fraction + s.fadeStep);
            if (s.fraction >= 1.0F) {
                setState(s.handle, kStateStop);
            }
        } else if (s.fadeMode == TagSpot::kFadingOut) {
            s.fraction = std::max(0.0F, s.fraction - s.fadeStep);
            if (s.fraction <= 0.0F) {
                setState(s.handle, kStateStop);
            }
        }
    }
}

const TagSpot* TagSpots::find(double tag) const {
    const auto found = std::ranges::find(m_spots, tag, &TagSpot::handle);
    return found == m_spots.end() ? nullptr : &*found;
}

} // namespace coney::world_objects
