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
// The blink of the chosen slot after an order (WarCommandDisplay_BlinkSlot): hidden while its counter is below 2
// (`0x0050d24c`); the update that finds it at twice that sets it back to 0 before counting.
constexpr int kBlinkHidden = 2;

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
// The slots' icons: their `part_page0` rectangles, as WarCommandDisplay_Init's sprite words name them.
constexpr std::array<std::uint32_t, kWarCommandSlots> kSlotIcons{0x56, 0x5a, 0x58, 0x55, 0x59, 0x5b};
// The plates' and backings' sprite word: big_font's rectangle 256, a white disc
// (docs/research/hud.md#war-command-disc).
constexpr std::uint32_t kDiscWord = 0xd0100;
// The resource instances the sprites are set up over: big_font (6, also font slot 6) and part_page0 (3).
constexpr int kBigFontInstance = 6;
constexpr int kPartsInstance = 3;
// The three sprites' sizes (WarCommandDisplay_Update): the backing 0.07 x 1.1 (`0x0050d304` x `0x0050d314`), the plate
// 0.07, the icon 0.08 (`0x0050d210`); each as wide as its rectangle's texels make it at that height.
constexpr float kBackingSize = 0.077F;
constexpr float kPlateSize = 0.07F;
constexpr float kIconSize = 0.08F;
// The name's line.
constexpr float kNameY = 0.86F;
constexpr int kNameFontSlot = 3;
// The text-table entries that replace the name: all commands locked, the highlighted one disabled.
constexpr std::size_t kAllLockedEntry = 6;
constexpr std::size_t kDisabledEntry = 7;
// The colours (WarCommandDisplay_Render; the words WarCommandLayout_StaticInit sets): gold `0x006007a0`, grey
// `0x006007a8` (the lit backing and every unlit plate), dark `0x006007b8` (a disabled command), the name's grey
// `0xff6e6e6e`.
constexpr graphics::Rgba kWhite{255, 255, 255, 255};
constexpr graphics::Rgba kGold{255, 183, 0, 255};
constexpr graphics::Rgba kGrey{35, 35, 35, 255};
constexpr graphics::Rgba kDark{30, 30, 30, 255};
constexpr graphics::Rgba kBlack{0, 0, 0, 255};
constexpr graphics::Rgba kNameGrey{110, 110, 110, 255};

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

// The batch over resource instance `instance`'s sheet: big_font's (font slot 6) or part_page0's; null for any other,
// or when the canvas has none.
// @coney-only glue: the original's resource instances are Coney's HUD batches
graphics::SpriteBatch* batchOf(const HudCanvas& canvas, int instance) {
    if (instance == kBigFontInstance) {
        // Stub for the test commit: the batch font slot 6 hands, whatever it is.
        return canvas.text.textBatch ? canvas.text.textBatch(gui::kBigFontSlot) : nullptr;
    }
    return instance == kPartsInstance ? canvas.parts : nullptr;
}

} // namespace

std::optional<int> warCommandOfSlot(std::size_t slot) {
    constexpr std::array<int, kWarCommandSlots> kCommands{0, 2, 4, 3, 5, 1};
    if (slot >= kWarCommandSlots) {
        return std::nullopt;
    }
    return kCommands.at(slot);
}

WarCommandSlotSprites warCommandSlotSprites(std::size_t slot) {
    // Stand-in: the display's player (+0x40) is the caller's (Look::centreX); the name's text (MessageHUD_Setup) is
    // laid out by render() from fixed values, and its box widget (+0x240), which Render never draws, is not kept.
    // Coney's choice: the setup's places, colours, size 0.08 and depths are not kept: Update places and sizes every
    // sprite each frame (kSlotPlaces and the sizes above), Render colours each as it draws, and the depths are unused
    // with an instance given.
    const std::size_t index = slot < kWarCommandSlots ? slot : 0;
    WarCommandSlotSprites sprites;
    sprites.icon = {.word = kSlotIcons.at(index), .instance = kPartsInstance};
    sprites.plate = {.word = kDiscWord, .instance = kBigFontInstance};
    sprites.backing = {.word = kDiscWord, .instance = kBigFontInstance};
    return sprites;
}

std::optional<float> warCommandStickAngle(std::uint8_t x, std::uint8_t y) {
    const int cx = static_cast<int>(x) - 127;
    const int cy = static_cast<int>(y) - 127;
    if ((cx * cx) + (cy * cy) <= kDeadZoneSquared) {
        return std::nullopt;
    }
    const float dx = static_cast<float>(x) - kAxisCentre;
    const float dy = static_cast<float>(y) - kAxisCentre;
    // The quadrant's base: x of 128 or more is right, y of 128 or less is up.
    const bool right = x >= 128;
    const bool up = y <= 128;
    float base = 180.0F;
    if (right) {
        base = up ? 0.0F : 90.0F;
    } else if (up) {
        base = 270.0F;
    }
    // The axis read: y where both bytes are 128 or more or both 128 or less, else x. The two tests overlap at 128,
    // so straight up at x 128 reads 90° (between slots) and full right with y on 128 reads 0°, as in the original.
    const bool readY = (x >= 128 && y >= 128) || (x <= 128 && y <= 128);
    const float angle = base + axisAngle(readY ? dy : dx);
    // 270 + 90 is straight up again.
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
    // A menu that is not allowed is left as it is, whoever opens it. Stand-in: Coney's display is always set up
    // (vtable +0x4c).
    if (!chief.allowed) {
        return;
    }
    // An allowed one hides for a human who leads no crew. Coney's choice: the caller gives player 1's display and
    // chief (the player found by his pad, the pad's index: GameplayMode); its sprites always exist and are resident
    // (+0x10); +0x1550 is not kept, and the two-player x is not built (stand-in: centreX stays 0.5).
    if (!chief.warChief) {
        m_shown = false;
        return;
    }
    if (!m_shown || m_issued) {
        m_textSince = nowMs;
    }
    m_shown = true;
    m_issued = false;
    m_afterIssue = 0;
    m_blink = 0;
    m_blinkHidden = false;
    m_fadeStart.reset();
    m_cameraStickOn = false;
}

std::optional<int> WarCommandDisplay::issue() {
    if (!m_shown || m_issued) {
        return std::nullopt;
    }
    // **Stand-in**: a disabled command's name is not shut down here (MessageHUD_Shutdown); render() draws nothing
    // after an order of a disabled command, so it would not show anyway. The slot kept at +0x1544 is the highlight,
    // and 0x0051480c (set to 0xff, its reader not traced) is not kept.
    m_issued = true;
    m_lit = true;
    m_afterIssue = 0;
    return warCommandOfSlot(m_highlight);
}

void WarCommandDisplay::update(std::uint8_t stickX, std::uint8_t stickY, const Chief& chief, std::uint64_t nowMs,
                               const HudSound& audio) {
    // Stand-ins: the chief's knock-out closes the menu from GameplayMode (close()); the sprites are always resident
    // (+0x10); the out-of-range byte (+0x414) is not kept (#1520), so render() picks entry 6 from Look::allLocked;
    // the name's text and places are render()'s, from fixed values (the box widget, never drawn, is not kept); +0x1558
    // (render's flag) is not kept.
    m_allowed = chief.allowed;
    if (!m_shown) {
        return;
    }
    // Picking: the name is set again, and the stick past the dead zone lights the slot and moves the highlight.
    if (!m_issued) {
        if (chief.menuLocked) {
            m_issued = true;
            m_afterIssue = 0;
            return;
        }
        m_nameShut = false;
        // The name's text is set again on every picking update (MarkupText_SetText clears its end time), so its
        // display time counts from the last one before the order.
        m_textSince = nowMs;
        if (const std::optional<float> angle = warCommandStickAngle(stickX, stickY); angle) {
            m_lit = true;
            const std::size_t slot = warCommandSlotAt(*angle, m_highlight);
            if (slot != m_highlight) {
                m_highlight = slot;
                audio.playCue(kWarCommandCue);
            }
        }
        return;
    }
    // After the order: the hold counts up, and once it is over the camera's stick comes back; the fade starts on the
    // update the hold reaches its end.
    if (m_afterIssue < kWarCommandHoldUpdates) {
        ++m_afterIssue;
    } else {
        m_cameraStickOn = true;
    }
    if (m_afterIssue >= kWarCommandHoldUpdates && !m_fadeStart) {
        m_fadeStart = nowMs;
    }
    // The chosen slot's blink, from the first update after the order.
    m_blinkHidden = m_blink < kBlinkHidden;
    if (!m_blinkHidden && m_blink == kBlinkHidden * 2) {
        m_blink = 0;
    }
    ++m_blink;
    // The display closes, unlit, once the highlighted name's `<DISPLAYTIME>` has run out (MarkupText_IsExpired),
    // tested on every update after the order.
    if (chief.nameDisplayMs && nowMs - std::min(m_textSince, nowMs) >= *chief.nameDisplayMs) {
        m_shown = false;
        m_lit = false;
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
    // Nothing while hidden or not allowed, nor after an order of a disabled command. **Stand-ins**: the chief's
    // knock-out closes the menu instead (GameplayMode), and Coney's sheets are always resident (`+0x10`).
    if (!m_shown || !m_allowed) {
        return;
    }
    const std::optional<int> chosen = warCommandOfSlot(m_highlight);
    const bool chosenEnabled = !look.enabled || (chosen && look.enabled(*chosen));
    if (m_issued && !chosenEnabled) {
        return;
    }
    const float others = fadeOf(false, nowMs);
    // Adds one of a slot's sprites centred at GUI (x, y), `height` tall and as wide as its rectangle's texels make it
    // at that height (BaseWidget_UpdateSize, no aspect fix); its rectangle is the word's low 16 bits, as Page_Rect
    // reads it. Nothing when its batch or rectangle is missing.
    const auto addSprite = [&canvas](const WarCommandSprite& sprite, float x, float y, float height,
                                     graphics::Rgba colour) {
        graphics::SpriteBatch* batch = batchOf(canvas, sprite.instance);
        const std::size_t rect = sprite.word & 0xffffU;
        if (batch == nullptr || rect >= batch->sheet().page.rects.size()) {
            return;
        }
        addRect(batch, rect, x, y, squareTexelWidth(batch->sheet(), batch->sheet().page.rect(rect), height), height,
                colour);
    };
    // The six slots, back to front: the backing, the plate, the icon; the chosen one blinks after an order.
    for (std::size_t slot = 0; slot < kWarCommandSlots; ++slot) {
        const bool isChosen = slot == m_highlight;
        if (isChosen && m_issued && m_blinkHidden) {
            continue;
        }
        // Lit: the highlighted slot once the stick or an order has lit it; the chosen slot's icon fades the longer.
        const bool lit = isChosen && m_lit;
        const float iconFade = isChosen ? fadeOf(true, nowMs) : others;
        const float x = look.centreX + kSlotPlaces.at(slot)[0];
        const float y = kSlotPlaces.at(slot)[1];
        const std::optional<int> command = warCommandOfSlot(slot);
        const bool enabled = !look.enabled || (command && look.enabled(*command));
        // The backing and plate are big_font's disc, under the part_page0 icon (the big font's batch draws first).
        const WarCommandSlotSprites sprites = warCommandSlotSprites(slot);
        addSprite(sprites.backing, x, y, kBackingSize, faded(lit ? kGrey : kBlack, others));
        addSprite(sprites.plate, x, y, kPlateSize, faded(lit ? kGold : kGrey, others));
        addSprite(sprites.icon, x, y, kIconSize, faded(enabled ? kWhite : kDark, iconFade));
    }
    // The highlighted slot's name, until its time runs out or its text is shut down. Its box widget (+0x240) is laid
    // out by Update but never drawn: Render draws only the text (+0x50) and the slots.
    if (!look.name || !canvas.text.fonts || m_nameShut) {
        return;
    }
    std::size_t entry = m_highlight;
    if (look.allLocked) {
        entry = kAllLockedEntry;
    } else if (!chosenEnabled) {
        entry = kDisabledEntry;
    }
    const std::string name = look.name(entry);
    gui::TextStyle style;
    style.x = look.centreX;
    style.y = kNameY;
    style.alignment = gui::TextAlignment::Centre;
    style.fontSlot = kNameFontSlot;
    // Gold after the order; grey while picking, dark for a disabled command.
    if (m_issued) {
        style.colour = kGold;
    } else {
        style.colour = chosenEnabled ? kNameGrey : kDark;
    }
    // The text's +0x18c is set to 255 on every draw: the name stays whole while the slots fade (seen in PCSX2).
    style.timeMs = static_cast<std::uint32_t>(nowMs - std::min(m_textSince, nowMs));
    // Centred on the menu's centre x, the text's anchor: the command texts open with <CENTER>, which the original's
    // layout (MessageHUD_Layout 0x001b9600) turns into a start half the widest line left of the anchor. **Coney's
    // reading**: gui::layoutText centres a line inside the box starting at style.x, so the box is placed by a first
    // layout's width; the name is always centred (every GSTRING.COMMAND entry opens with <CENTER>).
    const float width = gui::layoutText(name, style, canvas.text.fonts).width;
    style.x = look.centreX - (width / 2.0F);
    const gui::TextLayout text = gui::layoutText(name, style, canvas.text.fonts);
    if (text.expired) {
        return;
    }
    gui::addTextSprites(text, canvas.text.textBatch);
}

} // namespace coney::hud
