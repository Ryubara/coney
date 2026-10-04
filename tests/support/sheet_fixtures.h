// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic sprite sheets, built byte by byte in the tests. Nothing here comes from the game (LEGAL.md, "No game
// data"). The layouts are those of docs/research/gui.md#particle-page and the sheet table after it.

#include <bit>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "graphics/render_device.h"
#include "support/fixtures.h"

namespace coney::test {

/// Appends a float as the little-endian bytes of its IEEE 754 bits.
inline Bytes& f32(Bytes& bytes, float value) { return bytes.u32(std::bit_cast<std::uint32_t>(value)); }

/// The data of a 0x4C chunk: a placeholder word, the count, `firstGlyph`, two zero words, `rects`, then zero padding
/// to a multiple of 16 bytes.
inline Bytes particlePage(std::int32_t firstGlyph, std::initializer_list<graphics::UvRect> rects) {
    Bytes bytes;
    bytes.u32(0x12345678).u32(static_cast<std::uint32_t>(rects.size())).u32(static_cast<std::uint32_t>(firstGlyph));
    bytes.u32(0).u32(0);
    for (const graphics::UvRect& rect : rects) {
        f32(bytes, rect.u0);
        f32(bytes, rect.v0);
        f32(bytes, rect.u1);
        f32(bytes, rect.v1);
    }
    bytes.padTo((bytes.size() + 15) / 16 * 16);
    return bytes;
}

} // namespace coney::test
