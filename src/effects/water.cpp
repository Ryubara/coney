// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/water.h"

#include <cmath>
#include <cstddef>

namespace coney::effects {

Water::Water() : m_vertices(static_cast<std::size_t>(kColumns * kRows)) {}

void Water::set(const WaterSettings& settings) {
    m_settings = settings;
    m_settings->rotation = anim::normalise(settings.rotation); // all zeros: none
    update();
    m_lastUpdate = m_phase;
}

void Water::step() {
    if (!m_settings) {
        return;
    }
    m_phase += kPhasePerFrame;
    if (m_phase - m_lastUpdate > kUpdateEvery) {
        update();
        m_lastUpdate = m_phase;
    }
}

const std::vector<std::uint16_t>& Water::indices() {
    // Built once: each cell (i, j) as the triangles (a, b, c) and (b, d, c) over its corners a (i, j), b (i + 1, j),
    // c (i, j + 1), d (i + 1, j + 1). The water is drawn with culling off, so the winding does not matter.
    static const std::vector<std::uint16_t> list = [] {
        std::vector<std::uint16_t> out;
        out.reserve(static_cast<std::size_t>((kColumns - 1) * (kRows - 1) * 6));
        for (int j = 0; j + 1 < kRows; ++j) {
            for (int i = 0; i + 1 < kColumns; ++i) {
                const auto a = static_cast<std::uint16_t>(j * kColumns + i);
                const auto b = static_cast<std::uint16_t>(a + 1);
                const auto c = static_cast<std::uint16_t>(a + kColumns);
                const auto d = static_cast<std::uint16_t>(c + 1);
                out.insert(out.end(), {a, b, c, b, d, c});
            }
        }
        return out;
    }();
    return list;
}

anim::Vec3 Water::toWorld(anim::Vec3 p) const {
    if (!m_settings) {
        return p;
    }
    const anim::Vec3 scaled{p.x * m_settings->width, p.y * m_settings->length, p.z};
    return anim::transformPoint(anim::transform(m_settings->rotation, m_settings->position), scaled);
}

void Water::update() {
    const WaterSettings& s = *m_settings;
    const float t = m_phase * s.waveSpeed;
    const float u0 = std::fmod(t * kScrollRate, kRepeatsAcross);
    // Each column but the last: its wave, its alpha and its texture coordinates; the last copies the first's wave.
    for (int i = 0; i < kColumns; ++i) {
        const int waveColumn = i + 1 < kColumns ? i : 0;
        const float wave = std::sin(static_cast<float>(waveColumn) + t);
        const auto alpha = static_cast<std::uint8_t>(kBaseAlpha + kAlphaSwing * wave);
        const float u = u0 + kRepeatsAcross * static_cast<float>(i) / static_cast<float>(kColumns - 1);
        for (int j = 0; j < kRows; ++j) {
            WaterVertex& vertex = m_vertices[static_cast<std::size_t>(j * kColumns + i)];
            vertex.position = anim::Vec3{static_cast<float>(i) / static_cast<float>(kColumns - 1),
                                         static_cast<float>(j) / static_cast<float>(kRows - 1), wave * s.waveHeight};
            vertex.colour = {s.colour[0], s.colour[1], s.colour[2], alpha};
            vertex.u = u;
            vertex.v = kRepeatsAlong * static_cast<float>(j) / static_cast<float>(kRows - 1);
        }
    }
}

} // namespace coney::effects
