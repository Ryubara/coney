// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/war_command_display.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "graphics/render_device.h"
#include "gui/text_layout.h"

namespace coney::hud {

namespace {

// The dead zone: the squared distance from (127, 127) the stick must pass (radius 110 of 127).
constexpr int kDeadZoneSquared = 12100;
// The axis offsets' centre and scale of the one-axis angle.
constexpr float kAxisCentre = 128.0F;
// The hysteresis: how far the highlighted sector reaches into its neighbours (π/16).
constexpr float kHysteresisDegrees = 11.25F;
// The blink of the chosen slot after an order: hidden 2 of every 4 updates.
constexpr std::uint64_t kBlinkPeriod = 4;
constexpr std::uint64_t kBlinkHidden = 2;

// The slots' sectors, degrees clockwise from up (slot 0's wraps through 0); straight left and right lie between.
struct Sector {
    float from;
    float to;
};
constexpr std::array<Sector, kWarCommandSlots> kSectors{
    {{337.5F, 22.5F}, {22.5F, 87.75F}, {92.25F, 157.5F}, {157.5F, 202.5F}, {202.5F, 267.75F}, {272.25F, 337.5F}}};

// The slots' centres relative to (centre x, 0), default video mode (WarCommandDisplay_Place).
constexpr std::array<std::array<float, 2>, kWarCommandSlots> kSlotPlaces{
    {{0.0F, 0.795F}, {0.069F, 0.795F}, {0.071F, 0.93F}, {0.0F, 0.93F}, {-0.071F, 0.93F}, {-0.069F, 0.795F}}};
// The slots' icons in `part_page0`.
constexpr std::array<std::size_t, kWarCommandSlots> kSlotIcons{0x56, 0x5a, 0x58, 0x55, 0x59, 0x5b};
// The three sprites' sizes: the backing, the plate and the icon.
constexpr float kBackingSize = 0.077F;
constexpr float kPlateSize = 0.07F;
constexpr float kIconSize = 0.08F;
// The name: its line, its box (part_page0 rectangle 0x4e) and the box's margin.
constexpr float kNameY = 0.86F;
constexpr std::size_t kNameBoxRect = 0x4e;
constexpr float kNameBoxMargin = 0.013F;
constexpr int kNameFontSlot = 3;
// The text-table entries that replace the name: all commands locked, the highlighted one disabled.
constexpr std::size_t kAllLockedEntry = 6;
constexpr std::size_t kDisabledEntry = 7;
// The colours (WarCommandDisplay_Render).
constexpr graphics::Rgba kIconColour{255, 255, 255, 255};
constexpr graphics::Rgba kDisabledIconColour{30, 30, 30, 255};
constexpr graphics::Rgba kHighlightPlate{255, 183, 0, 255};
constexpr graphics::Rgba kPlate{35, 35, 35, 255};
constexpr graphics::Rgba kHighlightBacking{128, 128, 128, 255};
constexpr graphics::Rgba kBacking{0, 0, 0, 255};
constexpr graphics::Rgba kNameColour{191, 191, 191, 255};
constexpr graphics::Rgba kNameBox{0, 0, 0, 143};
// A GUI width that draws as square as a GUI height (a GUI width is stretched by 640 / 448).
constexpr float kSquareWidth = 448.0F / 640.0F;

// Whether `angle` lies in `sector`, `reach` degrees wider at each end.
bool inSector(float angle, const Sector& sector, float reach) {
    const float from = sector.from - reach;
    const float to = sector.to + reach;
    if (sector.from > sector.to) {
        return angle >= from || angle < to;
    }
    return (angle >= from && angle < to) || (from < 0.0F && angle >= from + 360.0F) ||
           (to > 360.0F && angle < to - 360.0F);
}

// asin(|d| / 128) in degrees, 90 for an offset of 128 or more.
float axisAngle(float offset) {
    const float ratio = std::fabs(offset) / kAxisCentre;
    return ratio >= 1.0F ? 90.0F : std::asin(ratio) * 180.0F / std::numbers::pi_v<float>;
}

} // namespace

std::optional<int> warCommandOfSlot(std::size_t slot) {
    constexpr std::array<int, kWarCommandSlots> kCommands{0, 2, 4, 3, 5, 1};
    if (slot >= kWarCommandSlots) {
        return std::nullopt;
    }
    return kCommands.at(slot);
}

std::optional<float> warCommandStickAngle(std::uint8_t x, std::uint8_t y) {
    const int cx = static_cast<int>(x) - 127;
    const int cy = static_cast<int>(y) - 127;
    if ((cx * cx) + (cy * cy) <= kDeadZoneSquared) {
        return std::nullopt;
    }
    const float dx = static_cast<float>(x) - kAxisCentre;
    const float dy = static_cast<float>(y) - kAxisCentre;
    const bool right = x >= 128;
    const bool down = y >= 128;
    if (right && !down) {
        return axisAngle(dx);
    }
    if (right && down) {
        return 90.0F + axisAngle(dy);
    }
    if (!right && down) {
        return 180.0F + axisAngle(dx);
    }
    // 270 + 90 is straight up again.
    const float angle = 270.0F + axisAngle(dy);
    return angle >= 360.0F ? angle - 360.0F : angle;
}

std::size_t warCommandSlotAt(float angle, std::size_t current) {
    if (current < kWarCommandSlots && inSector(angle, kSectors.at(current), kHysteresisDegrees)) {
        return current;
    }
    for (std::size_t slot = 0; slot < kWarCommandSlots; ++slot) {
        if (inSector(angle, kSectors.at(slot), 0.0F)) {
            return slot;
        }
    }
    return current;
}

void WarCommandDisplay::open(const Chief& chief, std::uint64_t nowMs) {
    if (!chief.warChief) {
        m_shown = false;
        return;
    }
    if (!chief.allowed) {
        return;
    }
    if (!m_shown || m_issued) {
        m_textSince = nowMs;
    }
    m_shown = true;
    m_issued = false;
    m_afterIssue = 0;
    m_fadeStart.reset();
    m_cameraStickOn = false;
}

std::optional<int> WarCommandDisplay::issue() {
    if (!m_shown || m_issued) {
        return std::nullopt;
    }
    m_issued = true;
    m_afterIssue = 0;
    return warCommandOfSlot(m_highlight);
}

void WarCommandDisplay::update(std::uint8_t stickX, std::uint8_t stickY, const Chief& chief, std::uint64_t nowMs,
                               const HudSound& audio) {
    ++m_updates;
    if (!m_shown) {
        return;
    }
    // Picking: the stick past the dead zone moves the highlight.
    if (!m_issued) {
        if (chief.menuLocked) {
            m_issued = true;
            m_afterIssue = 0;
            return;
        }
        if (const std::optional<float> angle = warCommandStickAngle(stickX, stickY); angle) {
            const std::size_t slot = warCommandSlotAt(*angle, m_highlight);
            if (slot != m_highlight) {
                m_highlight = slot;
                m_textSince = nowMs;
                audio.playCue(kWarCommandCue);
            }
        }
        return;
    }
    // After the order: the hold, then the camera's stick back and the fade; closed once the text has expired and the
    // fades are done.
    if (m_afterIssue < kWarCommandHoldUpdates) {
        ++m_afterIssue;
        return;
    }
    m_cameraStickOn = true;
    if (!m_fadeStart) {
        m_fadeStart = nowMs;
    }
    // The display closes once the highlighted name's `<DISPLAYTIME>` has run out (MarkupText_IsExpired).
    if (chief.nameDisplayMs && nowMs - std::min(m_textSince, nowMs) >= *chief.nameDisplayMs) {
        m_shown = false;
    }
}

void WarCommandDisplay::close() {
    m_shown = false;
    m_issued = false;
    m_cameraStickOn = true;
    m_fadeStart.reset();
}

float WarCommandDisplay::fadeOf(bool chosen, std::uint64_t nowMs) const {
    if (!m_fadeStart) {
        return 1.0F;
    }
    const auto length = static_cast<float>(chosen ? kWarCommandChosenFadeMs : kWarCommandFadeMs);
    const auto gone = static_cast<float>(nowMs - *m_fadeStart);
    return std::clamp(1.0F - (gone / length), 0.0F, 1.0F);
}

void WarCommandDisplay::render(const HudCanvas& canvas, const Look& look, std::uint64_t nowMs) const {
    if (!m_shown) {
        return;
    }
    const float others = fadeOf(false, nowMs);
    // The six slots, back to front: the backing, the plate, the icon; the chosen one blinks after an order.
    for (std::size_t slot = 0; slot < kWarCommandSlots; ++slot) {
        const bool lit = slot == m_highlight;
        if (lit && m_issued && m_afterIssue >= kWarCommandHoldUpdates && m_updates % kBlinkPeriod < kBlinkHidden) {
            continue;
        }
        const float fade = lit ? fadeOf(true, nowMs) : others;
        const float x = look.centreX + kSlotPlaces.at(slot)[0];
        const float y = kSlotPlaces.at(slot)[1];
        const std::optional<int> command = warCommandOfSlot(slot);
        const bool enabled = !look.enabled || (command && look.enabled(*command));
        if (canvas.flat != nullptr) {
            canvas.flat->addSprite(guiSprite(x, y, kBackingSize * kSquareWidth, kBackingSize, graphics::UvRect{},
                                             faded(lit ? kHighlightBacking : kBacking, fade)));
            canvas.flat->addSprite(guiSprite(x, y, kPlateSize * kSquareWidth, kPlateSize, graphics::UvRect{},
                                             faded(lit ? kHighlightPlate : kPlate, fade)));
        }
        if (canvas.parts != nullptr && kSlotIcons.at(slot) < canvas.parts->sheet().page.rects.size()) {
            const float width = squareTexelWidth(canvas.parts->sheet(),
                                                 canvas.parts->sheet().page.rect(kSlotIcons.at(slot)), kIconSize);
            addRect(canvas.parts, kSlotIcons.at(slot), x, y, width, kIconSize,
                    faded(enabled ? kIconColour : kDisabledIconColour, fade));
        }
    }
    // The highlighted slot's name over its box, until its time runs out.
    if (!look.name || !canvas.text.fonts) {
        return;
    }
    const std::optional<int> command = warCommandOfSlot(m_highlight);
    std::size_t entry = m_highlight;
    if (look.allLocked) {
        entry = kAllLockedEntry;
    } else if (look.enabled && (!command || !look.enabled(*command))) {
        entry = kDisabledEntry;
    }
    const std::string name = look.name(entry);
    gui::TextStyle style;
    style.x = look.centreX;
    style.y = kNameY;
    style.alignment = gui::TextAlignment::Centre;
    style.fontSlot = kNameFontSlot;
    style.colour = kNameColour;
    style.fade = others;
    style.timeMs = static_cast<std::uint32_t>(nowMs - std::min(m_textSince, nowMs));
    const gui::TextLayout text = gui::layoutText(name, style, canvas.text.fonts);
    if (text.expired) {
        return;
    }
    addRect(canvas.parts, kNameBoxRect, look.centreX, kNameY, text.width + kNameBoxMargin, text.height + kNameBoxMargin,
            faded(kNameBox, others));
    gui::addTextSprites(text, canvas.text.textBatch);
}

} // namespace coney::hud
