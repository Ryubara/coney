// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/glints.h"

#include <utility>

namespace coney::effects {

namespace {

// The distance within which a camera must see a triglint for it to keep its glints (`Cameras_IsPointVisibleAny(30,
// position)`).
constexpr float kVisibleDistance = 30.0F;

} // namespace

Triglint::Triglint(anim::Vec3 position) : m_position(position) { spawnGlints(); }

void Triglint::spawnGlints() {
    // A new glint's first update comes kGlintFirstUpdate ticks after it is made; one still there is kept as it is.
    for (Glint& glint : m_glints) {
        if (!glint.exists) {
            glint = Glint{.exists = true, .on = false, .countdown = kGlintFirstUpdate};
        }
    }
}

void Triglint::removeGlints() {
    for (Glint& glint : m_glints) {
        glint = Glint{};
    }
}

void Triglint::tick(const Visible& visible, const Random& random) {
    // The glints: each update toggles one, on for one interval and off for the next, the interval drawn afresh.
    for (Glint& glint : m_glints) {
        if (!glint.exists || --glint.countdown > 0) {
            continue;
        }
        glint.on = !glint.on;
        glint.countdown = kGlintIntervalMin + static_cast<int>(random(kGlintIntervalRandom));
    }
    // The triglint's own update: its glints only while a camera within 30 m sees its position.
    if (--m_countdown > 0) {
        return;
    }
    m_countdown = kInterval;
    if (visible && visible(m_position, kVisibleDistance)) {
        spawnGlints();
    } else {
        removeGlints();
    }
}

std::size_t Triglint::glints() const {
    std::size_t count = 0;
    for (const Glint& glint : m_glints) {
        count += glint.exists ? 1U : 0U;
    }
    return count;
}

void Triglint::addSprites(std::vector<Particle>& out) const {
    for (std::size_t i = 0; i < m_glints.size(); ++i) {
        if (!m_glints.at(i).exists || !m_glints.at(i).on) {
            continue;
        }
        // A sprite that neither moves nor fades: the glint's update switches it.
        Particle sprite;
        sprite.position = anim::add(m_position, kOffsets.at(i));
        sprite.size = kSizes.at(i) * kDrawnSize;
        sprite.colour = kOnColour;
        sprite.rect = kRect;
        sprite.fades = false;
        out.push_back(sprite);
    }
}

void Triglints::sync(std::span<const GlintOwner> owners) {
    // Keep (and move) the triglints whose owners are still listed, make the new ones; the rest are removed with their
    // glints (message 0x15).
    std::map<double, Triglint> kept;
    for (const GlintOwner& owner : owners) {
        auto found = m_triglints.find(owner.object);
        if (found != m_triglints.end()) {
            found->second.moveTo(owner.position);
            kept.emplace(owner.object, found->second);
        } else {
            kept.emplace(owner.object, Triglint(owner.position));
        }
    }
    m_triglints = std::move(kept);
}

void Triglints::tick(int ticks, const Triglint::Visible& visible) {
    const Triglint::Random random = [this](std::uint32_t n) { return draw(n); };
    for (int t = 0; t < ticks; ++t) {
        for (auto& [object, triglint] : m_triglints) {
            triglint.tick(visible, random);
        }
    }
}

std::vector<Particle> Triglints::sprites() const {
    std::vector<Particle> out;
    for (const auto& [object, triglint] : m_triglints) {
        triglint.addSprites(out);
    }
    return out;
}

const Triglint* Triglints::find(double object) const {
    const auto found = m_triglints.find(object);
    return found != m_triglints.end() ? &found->second : nullptr;
}

std::uint32_t Triglints::draw(std::uint32_t n) {
    // The next raw number, mod (n + 1), unsigned.
    return m_random.next() % (n + 1U);
}

} // namespace coney::effects
