// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/num_indicator.h"

#include <algorithm>

namespace coney::hud {

namespace {

// The marks' offsets from the origin without the header: x for the first group's strokes and bars, x for the later
// strokes, and y; the header's offset from the first mark; the marks' extra shift with the header (beside the header
// shift itself).
constexpr float kFirstGroupX = -0.095F;
constexpr float kLaterStrokeX = -0.100F;
constexpr float kMarkY = 0.104F;
constexpr GuiPoint kHeaderFromFirstMark{0.065F, -0.1F};
constexpr GuiPoint kHeaderMarkShift{0.05F, -0.1F};
// Marks in the first group (four strokes and the bar).
constexpr std::size_t kGroup = 5;

// The origin for `language` (NumIndicator_ApplyLayout: German moves it right).
GuiPoint originOf(Language language) {
    return language == Language::German ? GuiPoint{kNumOrigin.x + kNumOriginGermanShift, kNumOrigin.y} : kNumOrigin;
}

// The marks' shift right of the header for `language`.
float headerShiftOf(Language language) {
    return language == Language::German ? kNumHeaderShiftGerman : kNumHeaderShift;
}

} // namespace

GuiPoint NumIndicator::markPlace(std::size_t index, Language language) {
    const GuiPoint origin = originOf(language);
    const auto i = static_cast<float>(index);
    float x = 0.0F;
    if (isBar(index)) {
        x = origin.x + kFirstGroupX + (kTallyBarStep * i);
    } else if (index < kGroup) {
        x = origin.x + kFirstGroupX + (kTallyStrokeStep * i);
    } else {
        x = origin.x + kLaterStrokeX + (kTallyStrokeStep * i);
    }
    return GuiPoint{x + headerShiftOf(language) + kHeaderMarkShift.x, origin.y + kMarkY + kHeaderMarkShift.y};
}

GuiPoint NumIndicator::headerPlace(Language language) {
    const GuiPoint origin = originOf(language);
    return GuiPoint{origin.x + kFirstGroupX + kHeaderFromFirstMark.x, origin.y + kMarkY + kHeaderFromFirstMark.y};
}

std::uint32_t NumIndicator::headerRecord(Language language) {
    switch (language) {
    case Language::French:
        return kNumHeaderRecordFrench;
    case Language::German:
        return kNumHeaderRecordGerman;
    default:
        return kNumHeaderRecord;
    }
}

void NumIndicator::render(const HudCanvas& canvas, Language language) const {
    if (!on) {
        return;
    }
    // The header and its shadows, from the header's own sheet (the banners' batches serve any sheet-table record).
    if (canvas.banner) {
        const std::uint32_t record = headerRecord(language);
        graphics::SpriteBatch* header = canvas.banner(record, false);
        graphics::SpriteBatch* shadows = canvas.banner(record, true);
        if (header != nullptr && !header->sheet().page.rects.empty()) {
            const graphics::UvRect uv = header->sheet().page.rect(0);
            const float width = squareTexelWidth(header->sheet(), uv, kNumHeaderSize);
            const GuiPoint place = headerPlace(language);
            if (shadows != nullptr) {
                for (const GuiPoint& shift : kNumHeaderShadowOffsets) {
                    shadows->addSprite(
                        guiSprite(place.x + shift.x, place.y + shift.y, width, kNumHeaderSize, uv, graphics::kBlack));
                }
            }
            header->addSprite(guiSprite(place.x, place.y, width, kNumHeaderSize, uv, kNumIndicatorColour));
        }
    }
    // The marks, turned 3.3 rad. Only nine sprites exist, so a larger count draws nine (**Coney's choice**: the
    // original's loop would write past them).
    graphics::SpriteBatch* parts = canvas.parts;
    if (parts == nullptr || kTallyBarRect >= parts->sheet().page.rects.size()) {
        return;
    }
    const std::size_t marks = std::min<std::size_t>(count, kTallySprites);
    for (std::size_t i = 0; i < marks; ++i) {
        const bool bar = isBar(i);
        const graphics::UvRect uv = parts->sheet().page.rect(bar ? kTallyBarRect : kTallyStrokeRect);
        const float height = bar ? kTallyBarSize : kTallyStrokeSize;
        const GuiPoint place = markPlace(i, language);
        parts->addSprite(
            guiSprite(place.x, place.y, squareTexelWidth(parts->sheet(), uv, height), height, uv, kNumIndicatorColour),
            kTallyRotation);
    }
}

} // namespace coney::hud
