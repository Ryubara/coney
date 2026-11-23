// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/crew_status.h"

#include <algorithm>
#include <format>
#include <ranges>

#include "graphics/overlay_camera.h"
#include "graphics/screen.h"
#include "gui/base_widget.h"
#include "gui/text_layout.h"
#include "hud/messages.h"

namespace coney::hud {

namespace {

// The caption's format (`0x00553cb8`, an engine constant): its display time, size and centring, then the colour tag,
// the name and a space.
constexpr std::string_view kCaptionTags = "<DISPLAYTIME 2500><SIZE 1.0><CENTER>";
// The one name a caption spells out (`0x00553c90`, shown as `0x00553ca0`).
constexpr std::string_view kWarriorsSoldier = "WarriorsSoldier";
constexpr std::string_view kWarriorsSoldierShown = "Warriors Soldier";

// The portrait rectangles of the kinds 0-11, and of player slot 0's gang, slot 1's and neither.
constexpr std::array<std::uint32_t, 12> kKindPortraits{0x31, 0x28, 0x25, 0x23, 0x2e, 0x29,
                                                       0x33, 0x30, 0x27, 0x24, 0x2b, 0x2a};
constexpr std::array<std::uint32_t, kPlayers> kSlotPortraits{0x2c, 0x2d};
constexpr std::uint32_t kDefaultPortrait = 0x28;

// One portrait kind and the character types that have it (-1 pads a short list).
struct PortraitKind {
    int kind;
    std::array<int, 5> types;
};
// Human_GetPortraitKind's switch on the character type.
constexpr std::array<PortraitKind, 12> kPortraitKinds{{
    {0, {5, 7, 8, 9, 10}},
    {1, {15, 17, -1, -1, -1}},
    {2, {18, 20, 188, -1, -1}},
    {3, {11, 13, 14, -1, -1}},
    {4, {30, 32, -1, -1, -1}},
    {5, {21, 23, 24, 25, -1}},
    {6, {26, 28, 29, 191, -1}},
    {7, {33, 35, 36, 37, 190}},
    {8, {1, 3, 4, 189, -1}},
    {9, {38, 39, 40, -1, -1}},
    {10, {43, 44, -1, -1, -1}},
    {11, {45, 46, 47, 48, -1}},
}};

// The GSTRING.HUD caption of `state`. @orig 0x001a0420 CrewStatus_Add (unknown)
std::uint32_t captionString(CrewState state) {
    switch (state) {
    case CrewState::KnockedOut:
        return kCrewKnockedOutString;
    case CrewState::Cuffed:
        return kCrewCuffedString;
    default:
        return kCrewFightingString;
    }
}

// The status rectangle of `state` (not CrewState::None). @orig 0x001a0d28 CrewStatusEntry_Update (unknown)
std::uint32_t statusRect(CrewState state) {
    switch (state) {
    case CrewState::KnockedOut:
        return kCrewKnockedOutRect;
    case CrewState::Cuffed:
        return kCrewCuffedRect;
    default:
        return kCrewFightingRect;
    }
}

// The human of `handle` among `frame`'s; null when it no longer resolves. @orig 0x0019fda0 CrewStatus_Update (unknown)
const CrewMember* memberOf(const CrewFrame& frame, double handle) {
    const auto found = std::ranges::find(frame.humans, handle, &CrewMember::handle);
    return found == frame.humans.end() ? nullptr : &*found;
}

// An unsigned count as a float (a wrapped "less one" stays huge). @orig 0x0019fda0 CrewStatus_Update (unknown)
float unsignedFloat(std::uint32_t value) { return static_cast<float>(value); }

// A rectangle's texel width over its height (0 without a texture). @orig 0x001a0c90 CrewStatusEntry_Render (unknown)
float rectShape(const graphics::SpriteSheet& sheet, const graphics::UvRect& uv) {
    return squareTexelWidth(sheet, uv, 1.0F) * graphics::kLogicalWidth / graphics::kLogicalHeight;
}

} // namespace

CrewState crewState(const CrewMember& member) {
    // The fight alert: some brain holds him as an enemy, an attacker holds one of his slots and the camera is more
    // than 50 m away (the square of the distance over 2500).
    const bool fightingFar = member.hunted && member.attacked &&
                             member.cameraDistance * member.cameraDistance > kCrewFightDistance * kCrewFightDistance;
    if (member.knockedOut) {
        return CrewState::KnockedOut;
    }
    if (member.cuffed) {
        return CrewState::Cuffed;
    }
    return fightingFar ? CrewState::FightingFar : CrewState::None;
}

std::optional<int> crewPortraitKind(int type) {
    for (const PortraitKind& kind : kPortraitKinds) {
        if (std::ranges::contains(kind.types, type)) {
            return kind.kind;
        }
    }
    // Every other type has a kind of 12 or more (the language banners' byte plus 20), which no portrait has.
    return std::nullopt;
}

std::uint32_t crewPortraitRect(const CrewMember& member, const CrewFrame& frame) {
    // Outside co-op, the Warrior's own portrait.
    if (!frame.coop) {
        if (const std::optional<int> kind = crewPortraitKind(member.characterType); kind) {
            return kKindPortraits.at(static_cast<std::size_t>(*kind));
        }
    }
    // Otherwise whose gang he is in: the first player slot whose gang is his.
    for (std::size_t slot = 0; slot < kPlayers; ++slot) {
        const int gang = frame.playerGangs.at(slot);
        if (gang >= 0 && gang == member.gang) {
            return kSlotPortraits.at(slot);
        }
    }
    return kDefaultPortrait;
}

std::string crewCaptionName(std::string_view name) {
    std::string shown(name);
    // The first byte above '`' moves down by 0x20: a lower-case letter becomes a capital.
    if (!shown.empty() && static_cast<unsigned char>(shown.front()) > 0x60) {
        shown.front() = static_cast<char>(static_cast<unsigned char>(shown.front()) - 0x20);
    }
    if (shown == kWarriorsSoldier) {
        shown = kWarriorsSoldierShown;
    }
    return shown;
}

std::string crewCaption(const CrewMember& member, CrewState state, const CrewTexts& texts) {
    if (state == CrewState::None) {
        return {};
    }
    const std::string colour = texts.hudColour ? texts.hudColour(kCrewCaptionColourSlot) : std::string{};
    const std::string words = texts.hudString ? texts.hudString(captionString(state)) : std::string{};
    return std::format("{}{}{} {}", kCaptionTags, colour, crewCaptionName(member.name), words);
}

float crewStatusAlpha(std::uint64_t nowMs) {
    // A triangle wave over 800 ms: falling from 400 to 0 in the first half, rising from 0 to 400 in the second.
    constexpr std::uint64_t kHalf = kCrewPulseMs / 2;
    const std::uint64_t phase = nowMs % kCrewPulseMs;
    const std::uint64_t wave = phase < kHalf ? kHalf - phase : phase - kHalf;
    const float share = static_cast<float>(wave) / static_cast<float>(kHalf);
    // The full colour blended toward none by the cube: Colour_LerpPacked(t, full, clear) is full × (1 − t).
    return 1.0F - (share * share * share);
}

bool CrewStatusList::add(const CrewMember& member, const CrewFrame& frame, const CrewTexts& texts,
                         std::uint64_t nowMs) {
    // Only while a state holds.
    const CrewState state = crewState(member);
    if (state == CrewState::None) {
        return false;
    }
    // Refused: outside slot 0's gang with neither a player's (0) nor a Warrior's (3) brain, and the bums.
    constexpr int kPlayerBrain = 0;
    constexpr int kWarriorBrain = 3;
    if (member.gang != frame.playerGangs.at(0) && member.brainType != kWarriorBrain &&
        member.brainType != kPlayerBrain) {
        return false;
    }
    if (member.characterType >= kCrewFirstBum && member.characterType <= kCrewLastBum) {
        return false;
    }
    // Listed already: nothing more.
    if (find(member.handle) != nullptr) {
        return true;
    }
    // The first free slot takes him at the next order, his portrait chosen now, the status icon the cuffed one until
    // its first update; the caption becomes his.
    const auto slot = std::ranges::find_if(m_entries, [](const Entry& entry) { return !entry.order.has_value(); });
    if (slot == m_entries.end()) {
        return false;
    }
    *slot = Entry{.order = static_cast<std::uint32_t>(m_count),
                  .handle = member.handle,
                  .shown = true,
                  .portrait = crewPortraitRect(member, frame),
                  .status = kCrewCuffedRect,
                  .place = {}};
    ++m_count;
    m_caption = crewCaption(member, state, texts);
    m_captionSinceMs = nowMs;
    return true;
}

bool CrewStatusList::remove(double handle) {
    // The listed slot holding the handle is freed, and every later order moves up one. **Coney's stand-in**: the
    // original matches any slot; only a listed one does here, as a free slot's handle 0 is also an unbound brain's.
    const auto slot = std::ranges::find_if(
        m_entries, [handle](const Entry& entry) { return entry.order.has_value() && entry.handle == handle; });
    if (slot == m_entries.end()) {
        return false;
    }
    const std::uint32_t gone = slot->order.value_or(0U); // the find above only takes a listed slot
    *slot = Entry{};
    --m_count;
    for (Entry& entry : m_entries) {
        if (entry.order && *entry.order > gone) {
            --*entry.order;
        }
    }
    return true;
}

GuiPoint CrewStatusList::entryPlace(std::uint32_t order, std::size_t count) {
    const auto perRow = static_cast<std::uint32_t>(kCrewLayout.perRow);
    GuiPoint place{kCrewLayout.origin.x + (unsignedFloat(order % perRow) * kCrewLayout.columnStep),
                   kCrewLayout.origin.y + (unsignedFloat(order / perRow) * kCrewLayout.rowStep)};
    // A row of fewer than four moves right toward the middle.
    if (count < kCrewLayout.perRow) {
        place.x += kCrewLayout.shortRowShift +
                   (unsignedFloat(static_cast<std::uint32_t>(count) - 1U) * kCrewLayout.shortRowStep);
    }
    return place;
}

GuiPoint CrewStatusList::captionPlaceFor(std::size_t placed, std::size_t count) {
    const auto perRow = static_cast<std::uint32_t>(kCrewLayout.perRow);
    // Under the last row placed; a full row of four or eight counts as one for the x step. The counts are unsigned,
    // so with none placed or listed "less one" wraps and the caption lands far off the screen, as in the original.
    const float rows = unsignedFloat((static_cast<std::uint32_t>(placed) - 1U) / perRow) + 1.0F;
    const std::uint32_t across = (count == 4 || count == 8) ? 1U : static_cast<std::uint32_t>(count);
    return GuiPoint{kCrewLayout.origin.x + kCrewLayout.captionX +
                        (unsignedFloat((across & 3U) - 1U) * kCrewLayout.captionStep),
                    kCrewLayout.origin.y + (rows * kCrewLayout.rowStep) + kCrewLayout.captionDrop};
}

void CrewStatusList::updateEntry(Entry& entry, const CrewMember& member, const CrewFrame& frame) {
    // Hidden unless he is in slot 0's gang and a state holds; else the state's icon.
    const CrewState state = member.gang == frame.playerGangs.at(0) ? crewState(member) : CrewState::None;
    if (state == CrewState::None) {
        entry.shown = false;
        return;
    }
    entry.status = statusRect(state);
}

void CrewStatusList::update(const CrewFrame& frame) {
    // Each slot in turn: a listed entry that is hidden or whose human is gone is removed (the later ones move up at
    // once, and the count with them); any other is placed at its cell for the count now and updated.
    std::size_t placed = 0;
    for (Entry& entry : m_entries) {
        if (!entry.order) {
            continue;
        }
        const CrewMember* member = entry.shown ? memberOf(frame, entry.handle) : nullptr;
        if (member == nullptr) {
            remove(entry.handle);
            continue;
        }
        entry.place = entryPlace(*entry.order, m_count);
        ++placed;
        updateEntry(entry, *member, frame);
    }
    // The caption under the last row placed.
    m_captionPlace = captionPlaceFor(placed, m_count);
}

void CrewStatusList::render(const HudCanvas& canvas, std::uint64_t nowMs) const {
    // Each shown entry's portrait, then its status icon at the portrait's lower left, pulsing.
    graphics::SpriteBatch* parts = canvas.parts;
    if (parts != nullptr) {
        const std::size_t rects = parts->sheet().page.rects.size();
        const graphics::Rgba status = faded(graphics::kWhite, crewStatusAlpha(nowMs));
        for (const Entry& entry : m_entries) {
            if (!entry.order || !entry.shown || entry.portrait >= rects || entry.status >= rects) {
                continue;
            }
            const graphics::UvRect portrait = parts->sheet().page.rect(entry.portrait);
            const graphics::UvRect icon = parts->sheet().page.rect(entry.status);
            const CrewEntrySprites at =
                crewEntrySprites(entry.place, rectShape(parts->sheet(), portrait), rectShape(parts->sheet(), icon));
            parts->addSprite(guiSprite(at.portrait.centre.x, at.portrait.centre.y, at.portrait.width,
                                       at.portrait.height, portrait, kCrewPortraitColour));
            parts->addSprite(
                guiSprite(at.status.centre.x, at.status.centre.y, at.status.width, at.status.height, icon, status));
        }
    }
    // The caption, timed from when it was set: its own `<DISPLAYTIME>` fades it and ends it.
    if (m_caption.empty() || nowMs < m_captionSinceMs) {
        return;
    }
    gui::TextStyle style = messageStyle(m_captionPlace.x);
    style.colour = kCrewCaptionColour;
    style.timeMs = static_cast<std::uint32_t>(nowMs - m_captionSinceMs);
    drawMessage(canvas, m_caption, style, m_captionPlace.y);
}

const CrewStatusList::Entry* CrewStatusList::find(double handle) const {
    const auto found =
        std::ranges::find_if(m_entries, [handle](const Entry& entry) { return entry.order && entry.handle == handle; });
    return found == m_entries.end() ? nullptr : &*found;
}

CrewEntrySprites crewEntrySprites(GuiPoint place, float portraitShape, float statusShape) {
    // A sprite's width in GUI units: the height × the rectangle's shape × the aspect fix in overlay units, and an
    // overlay width is H / W GUI widths.
    constexpr float kWidthPerShape = graphics::OverlayCamera::kViewAspect * gui::BaseWidget::kAspectFix *
                                     graphics::kLogicalHeight / graphics::kLogicalWidth;
    const float portraitWidth = kCrewLayout.portraitSize * portraitShape * kWidthPerShape;
    const float statusWidth = kCrewLayout.statusSize * statusShape * kWidthPerShape;
    // The portrait's right edge is at the place (anchor 2); the icon's left edge at the place + the offset (anchor 1).
    return CrewEntrySprites{
        .portrait = {.centre = {place.x - (portraitWidth / 2.0F), place.y},
                     .width = portraitWidth,
                     .height = kCrewLayout.portraitSize},
        .status = {.centre = {place.x + kCrewLayout.statusOffset.x + (statusWidth / 2.0F),
                              place.y + kCrewLayout.statusOffset.y},
                   .width = statusWidth,
                   .height = kCrewLayout.statusSize},
    };
}

} // namespace coney::hud
