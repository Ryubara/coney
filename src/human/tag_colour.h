// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

// A human's tag paint colour (`+0x640`): four bytes that the tagging HUD and the spray mist read in opposite orders.
// Research: docs/research/crimes.md#tag-look

namespace coney::human {

/// The four bytes of human `+0x640` in memory order.
using TagColour = std::array<std::uint8_t, 4>;

/// Every human's colour from `Human_Init`: bytes `50 07 05 20`, a dark maroon (80, 7, 5) on the HUD and the mist
/// (32, 5, 7) at alpha 80.
/// @orig 0x00218770 Human_Init (unknown)
inline constexpr TagColour kDefaultTagColour{0x50, 0x07, 0x05, 0x20};
/// The colour `Human_Init` gives instead to a human whose class has brain kind 4 (a civilian) and `+0x3b8` = 1
/// (`CfgChar`'s women): bytes `ff ef ef ef`.
inline constexpr TagColour kFemaleCivilianTagColour{0xff, 0xef, 0xef, 0xef};

/// The bytes `HuTagColor(h, {t1, t2, t3, t4})` writes: t4, t3, t2, t1 in memory order.
/// @orig 0x00239080 Human_SetTagColour (unknown)
[[nodiscard]] constexpr TagColour tagColourFromScript(std::uint8_t t1, std::uint8_t t2, std::uint8_t t3,
                                                      std::uint8_t t4) {
    return TagColour{t4, t3, t2, t1};
}

/// The colour as the tagging HUD reads it (`TagHud_Render`): bytes 0-2 as r, g, b (it sets its own alpha).
[[nodiscard]] constexpr std::array<std::uint8_t, 3> tagHudRgb(const TagColour& colour) {
    return {colour[0], colour[1], colour[2]};
}

/// The colour as the spray mist reads it (`SprayMist_Init`): the bytes as one little-endian word `0xRRGGBBAA`, alpha
/// in byte 0, so the scripts' `{r, g, b, a}` read as written.
[[nodiscard]] constexpr std::uint32_t tagMistRgba(const TagColour& colour) {
    return static_cast<std::uint32_t>(colour[0]) | (static_cast<std::uint32_t>(colour[1]) << 8U) |
           (static_cast<std::uint32_t>(colour[2]) << 16U) | (static_cast<std::uint32_t>(colour[3]) << 24U);
}

} // namespace coney::human
