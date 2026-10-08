// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/language.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// The gang-count indicator's header: sheet-table record `0x20e` rectangle 0 (sprite word `0x20e0000`), French `0x20f`,
/// German `0x210`; its height (an overlay height, the width following the rectangle's aspect), colour and the black
/// shadows' offsets.
inline constexpr std::uint32_t kNumHeaderRecord = 0x20e;
inline constexpr std::uint32_t kNumHeaderRecordFrench = 0x20f;
inline constexpr std::uint32_t kNumHeaderRecordGerman = 0x210;
inline constexpr float kNumHeaderSize = 0.048F;
inline constexpr std::array<GuiPoint, 2> kNumHeaderShadowOffsets{GuiPoint{0.0035F, 0.005F}, GuiPoint{-0.002F, -0.003F}};
/// The tally marks: `part_page0` rectangle 366 (a short upright stroke) and 367 (the long bar crossing every fifth),
/// their heights, their colour (the header's too) and their turn (radians, clockwise on screen).
inline constexpr std::size_t kTallyStrokeRect = 366;
inline constexpr std::size_t kTallyBarRect = 367;
inline constexpr float kTallyStrokeSize = 0.048F;
inline constexpr float kTallyBarSize = 0.0204F;
inline constexpr graphics::Rgba kNumIndicatorColour{35, 83, 188, 255};
inline constexpr float kTallyRotation = 3.3F;
/// The mark sprites an indicator owns: the most it draws.
inline constexpr std::size_t kTallySprites = 9;

/// The shared indicator's layout in the default video mode (`NumIndicator_ApplyLayout`): the origin, German's shift of
/// it, the shift the header gives the marks (German's), and the marks' steps.
inline constexpr GuiPoint kNumOrigin{0.09F, 0.94F};
inline constexpr float kNumOriginGermanShift = 0.05F;
inline constexpr float kNumHeaderShift = 0.08F;
inline constexpr float kNumHeaderShiftGerman = 0.15F;
inline constexpr float kTallyStrokeStep = 0.015F;
inline constexpr float kTallyBarStep = 0.005F;

/// A gang-count indicator (`HUDSetNumIndicator`; the shared one is `NumIndicator`, HUD `+0x16b30`): a gang's living
/// members counted in blue tally marks, four strokes and a bar crossing them, after a header.
///
/// Research: docs/research/hud.md#gang-count-indicator
struct NumIndicator {
    bool on = false;         ///< `+0x04`.
    int gang = -1;           ///< `+0xc60`: the gang whose count it shows; -1 for none.
    std::uint32_t count = 0; ///< `+0xc40`: the marks to show, the gang's living members at the last update.

    /// Where tally mark `index` (0-based) goes, GUI: strokes 0.015 apart, every fifth a bar centred on the four before
    /// it, the whole row moved right of the header and up 0.1 (the header is always made, so this is the row the
    /// original shows). German moves the origin and the header's shift.
    /// @orig 0x001b67b8 NumIndicator_Update (unknown)
    [[nodiscard]] static GuiPoint markPlace(std::size_t index, Language language);
    /// Whether mark `index` is a crossing bar (every fifth).
    [[nodiscard]] static bool isBar(std::size_t index) { return index % 5 == 4; }
    /// Where the header goes, GUI: the first mark's place without the header, plus (0.065, -0.1).
    [[nodiscard]] static GuiPoint headerPlace(Language language);
    /// The header's sheet-table record for `language`.
    /// @orig 0x001b6358 NumIndicator_Setup (unknown)
    [[nodiscard]] static std::uint32_t headerRecord(Language language);

    /// Adds the indicator while on: the header's two black shadows and the header (canvas.banner, by its record), then
    /// `count` marks (at most kTallySprites) into canvas.parts.
    /// @orig 0x001b6ba0 NumIndicator_Render (unknown)
    void render(const HudCanvas& canvas, Language language) const;
};

} // namespace coney::hud
