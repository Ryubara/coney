// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/player_panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include "core/assert.h"

namespace coney::hud {

namespace {

// The rows of the counter-slot table: x from x0 and the line, for money 0 or less, 1-9, 10-99 and 100-999.
struct SlotSpec {
    float x;
    bool line2;
};
constexpr std::array<std::array<SlotSpec, 4>, 4> kSlotTable{{
    {{{-0.095F, false}, {-0.045F, false}, {0.005F, false}, {0.055F, false}}},
    {{{-0.045F, false}, {0.005F, false}, {0.055F, false}, {-0.09F, true}}},
    {{{-0.028F, false}, {0.02F, false}, {0.07F, false}, {-0.09F, true}}},
    {{{-0.005F, false}, {0.042F, false}, {-0.092F, true}, {-0.04F, true}}},
}};

// One banner kind's types and its sheet record.
struct BannerKind {
    std::array<int, 5> types;
    std::uint32_t record;
};
// The kinds of PlayerHUD_SetBanner by character type (-1 pads a short list).
constexpr std::array<BannerKind, 10> kBannerKinds{{
    {{5, 7, 8, 9, 10}, 0x31},
    {{0xf, 0x11, -1, -1, -1}, 0x22},
    {{0x12, 0x14, 0xbc, -1, -1}, 0x23},
    {{0xb, 0xd, 0xe, -1, -1}, 0x1f},
    {{0x1e, 0x20, -1, -1, -1}, 0x2f},
    {{0x15, 0x17, 0x18, 0x19, -1}, 0x24},
    {{0x1a, 0x1c, 0x1d, 0xbf, -1}, 0x32},
    {{0x21, 0x23, 0x24, 0x25, 0xbe}, 0x30},
    {{1, 3, 4, 0xbd, -1}, 0x21},
    {{0x26, 0x27, 0x28, -1, -1}, 0x20},
}};
constexpr std::uint32_t kDefaultBanner = 0x31;

} // namespace

std::optional<std::array<GuiPoint, 4>> counterSlots(int money) {
    std::size_t row = 0;
    if (money >= 1000) {
        return std::nullopt;
    }
    if (money >= 100) {
        row = 3;
    } else if (money >= 10) {
        row = 2;
    } else if (money >= 1) {
        row = 1;
    }
    std::array<GuiPoint, 4> slots{};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const SlotSpec& spec = kSlotTable.at(row).at(i);
        slots.at(i) = GuiPoint{kSlotX0 + spec.x, spec.line2 ? kSlotLine2 : kSlotLine1};
    }
    return slots;
}

std::uint32_t bannerRecord(int type) {
    for (const BannerKind& kind : kBannerKinds) {
        if (std::ranges::find(kind.types, type) != kind.types.end()) {
            return kind.record;
        }
    }
    return kDefaultBanner;
}

PlayerPanel::PlayerPanel(std::size_t player) : m_player(player) {
    CONEY_ASSERT(player < kPlayers);
    m_slots = counterSlots(0).value_or(std::array<GuiPoint, 4>{});
}

bool PlayerPanel::attach(int type) {
    if (m_attached) {
        return false;
    }
    m_attached = true;
    m_banner = bannerRecord(type);
    // The fade time 0 means "now": the panel shows for its hold and fade once attached.
    m_activityMs = m_nowMs;
    show();
    return true;
}

void PlayerPanel::show() { m_shown = m_attached && m_mayShow; }

bool promptWakesPanel(std::string_view text) {
    return std::ranges::any_of(std::array<std::string_view, 4>{"Spray", "Flash", "Blades", "Give Mon"},
                               [text](std::string_view word) { return text.find(word) != std::string_view::npos; });
}

void PlayerPanel::setFlashFrames(std::uint32_t frames) {
    m_flashFrames = frames;
    m_flashCounter = 0;
}

void PlayerPanel::update(const PanelValues& values, const Pad* pad, std::uint64_t nowMs, const HudSound& audio) {
    m_nowMs = nowMs;
    m_values = values;
    bool activity = m_forceShow;

    // The score and the money count toward their values; a change is activity and makes a popup.
    activity = m_score.update(values.score, nowMs) || activity;
    const int money = std::clamp(values.money, kMoneyMin, kMoneyMax);
    const bool wasCounting = m_money.counting();
    activity = m_money.update(money, nowMs) || activity;
    // Coney's choice: the counting cue plays once as the count starts, not every frame it runs.
    if (m_money.counting() && !wasCounting) {
        audio.playCue(kCueMoneyCount);
    }
    if (const std::optional<CountingNumber::Popup> popup = m_score.popup();
        popup && nowMs - popup->startMs >= kScorePopupMs) {
        m_score.clearPopup();
    }
    if (const std::optional<CountingNumber::Popup> popup = m_money.popup();
        popup && nowMs - popup->startMs >= kMoneyPopupMs) {
        m_money.clearPopup();
    }

    // The counters: a changed count is activity; the slots follow the money's digits below 1,000.
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const int count = std::clamp(values.items.at(i), 0, kCounterMax);
        if (count != m_items.at(i)) {
            m_items.at(i) = count;
            activity = true;
        }
    }
    if (const std::optional<std::array<GuiPoint, 4>> slots = counterSlots(money)) {
        m_slots = *slots;
    }

    // SELECT shows the panel when the player's record allows it.
    if (pad != nullptr && values.selectShows && pad->pressed(pad::kSelect)) {
        activity = true;
    }
    // A prompt naming a dealer's goods wakes it too.
    if (values.promptWakes) {
        activity = true;
    }
    if (activity) {
        m_activityMs = nowMs;
    }

    // A meter that has just filled plays the rage sound once.
    const bool full = values.rageMax > 0 && values.rage >= values.rageMax;
    if (full && !m_wasFull) {
        audio.playSound(kRageFullSound);
    }
    m_wasFull = full;
    ++m_flashCounter;
}

float PlayerPanel::fade() const {
    const std::uint64_t since = m_nowMs >= m_activityMs ? m_nowMs - m_activityMs : 0;
    if (since <= kPanelHoldMs) {
        return 1.0F;
    }
    if (since >= kPanelHoldMs + kPanelFadeMs) {
        return 0.0F;
    }
    return 1.0F - static_cast<float>(since - kPanelHoldMs) / static_cast<float>(kPanelFadeMs);
}

bool PlayerPanel::meterVisible() const { return m_flashFrames == 0 || (m_flashCounter / m_flashFrames) % 2 == 0; }

float PlayerPanel::fill() const {
    const int rage = std::min(m_values.rage, m_values.rageMax);
    return std::clamp(static_cast<float>(rage) / kRageFullScale, 0.0F, 1.0F);
}

float PlayerPanel::capacity() const {
    return std::clamp(static_cast<float>(m_values.rageMax) / kRageFullScale, 0.0F, 1.0F);
}

graphics::Rgba PlayerPanel::fillColour(bool swapped) const {
    const graphics::Rgba red = swapped ? kRageGold : kRageRed;
    const graphics::Rgba gold = swapped ? kRageRed : kRageGold;
    if (m_values.rageMax > 0 && m_values.rage >= m_values.rageMax) {
        // A triangle wave over 400 ms: gold while its cube is under the threshold, a short pulse at each trough.
        const auto t = static_cast<float>(m_nowMs % (std::uint64_t{2} * kRageFlashHalfPeriodMs));
        const float half = static_cast<float>(kRageFlashHalfPeriodMs);
        const float f = std::abs(half - t) / half;
        return f * f * f < kRageFlashThreshold ? gold : red;
    }
    return m_values.raging ? gold : red;
}

graphics::Rgba PlayerPanel::bannerColour(bool swapped) const {
    if (m_values.altBanner) {
        return kBannerAltColour;
    }
    const bool gold = m_values.raging;
    return gold != swapped ? kRageGold : kRageRed;
}

float PlayerPanel::tallyShift(int levelNumber) const {
    // The counters shown take the slots in order, so slot k holds an item when more than k counters show.
    const auto shown = static_cast<std::size_t>(std::ranges::count_if(m_items, [](int count) { return count >= 1; }));
    const int money = m_values.money;
    float shift = 0.0F;
    if ((money >= 1 && money <= 999) || shown > 0) {
        const auto onLine2 = [&](std::size_t slot) { return shown > slot && m_slots.at(slot).y == kSlotLine2; };
        shift = onLine2(2) || onLine2(3) ? kTallyShiftTwoLines : kTallyShiftOneLine;
    }
    if (levelNumber >= kArcadeLevelStart) {
        shift += kTallyArcadeShift;
    }
    return shift;
}

GuiPoint PlayerPanel::tallyMarkPlace(std::size_t index, int levelNumber) const {
    const GuiPoint base = kPanelBase.at(m_player);
    const auto i = static_cast<float>(index);
    float x = 0.0F;
    if (NumIndicator::isBar(index)) {
        x = kTallyFirstX.at(m_player) + (kTallyBarStep * i);
    } else if (index < 5) {
        x = kTallyFirstX.at(m_player) + (kTallyStrokeStep * i);
    } else {
        x = kTallyLaterX.at(m_player) + (kTallyStrokeStep * i);
    }
    return GuiPoint{base.x + x, base.y + kTallyY + tallyShift(levelNumber)};
}

GuiPoint PlayerPanel::shifted(GuiPoint offset) const {
    const GuiPoint base = kPanelBase.at(m_player);
    const float shift = m_player == 0 ? 0.0F : kPlayer1Shift;
    return GuiPoint{base.x + offset.x + shift, base.y + offset.y};
}

} // namespace coney::hud
