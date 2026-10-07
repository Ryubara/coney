// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "graphics/render_device.h"

// The in-game HUD's layout and colours in the default video mode (NTSC, interlaced, 4:3), as the original's static
// data holds them. Positions are GUI coordinates (x right, y down, [0, 1] across the safe area,
// graphics::OverlayCamera), points given as offsets from a player panel's base unless said otherwise.
// Research: docs/research/hud.md#data

namespace coney::hud {

/// A GUI point or offset.
struct GuiPoint {
    float x = 0.0F;
    float y = 0.0F;

    friend bool operator==(const GuiPoint&, const GuiPoint&) = default;
};

/// A GUI size.
struct GuiSize {
    float width = 0.0F;
    float height = 0.0F;
};

/// How many player panels the HUD has.
inline constexpr std::size_t kPlayers = 2;

// ---- The player panel (`0x0050fa10`) ----

/// The panels' bases: player 0 at the top left, player 1 at the top right.
inline constexpr std::array<GuiPoint, kPlayers> kPanelBase{GuiPoint{0.10F, 0.05F}, GuiPoint{0.785F, 0.05F}};
/// The name banner's anchor (its left edge and vertical centre) from the base, per player.
inline constexpr std::array<GuiPoint, kPlayers> kBannerOffset{GuiPoint{-0.105F, -0.005F}, GuiPoint{-0.015F, -0.005F}};
/// The banner's height: the table's single size 0.06 (Coney reads it as the height; the width follows the sheet).
inline constexpr float kBannerHeight = 0.06F;
/// The rage meter's left end from the base, per player, and its size.
inline constexpr std::array<GuiPoint, kPlayers> kMeterOffset{GuiPoint{-0.107F, 0.035F}, GuiPoint{-0.018F, 0.035F}};
inline constexpr GuiSize kMeterSize{0.30F, 0.009F};
/// The score's place from player 0's base.
inline constexpr GuiPoint kScoreOffset{-0.103F, 0.064F};
/// The money's place from player 0's base, and its icon's offset from the money.
inline constexpr GuiPoint kMoneyOffset{-0.095F, 0.104F};
inline constexpr GuiPoint kMoneyIconOffset{0.012F, -0.001F};
/// The glyph size (w, h) of the score and the counters.
inline constexpr GuiSize kPanelTextSize{0.04F, 0.05F};
/// **Coney's stand-in:** player 1's score, money and counters sit this much right of player 0's (the page gives only
/// the banner's and the meter's x for player 1, which differ from player 0's by about this).
inline constexpr float kPlayer1Shift = 0.09F;

/// The black copies under the banner: offsets and depths (`0x00510054`-`0x00510064`).
inline constexpr std::array<GuiPoint, 2> kBannerShadowOffsets{GuiPoint{0.0025F, 0.004F}, GuiPoint{-0.001F, -0.002F}};
inline constexpr float kBannerShadowDepth = 10000.0F;
inline constexpr float kBannerDepth = 11000.0F;

/// How long the panel stays opaque after the last activity, then how long it fades (`0x00510070`, `0x00510074`).
inline constexpr std::uint32_t kPanelHoldMs = 2000;
inline constexpr std::uint32_t kPanelFadeMs = 1000;

// ---- Colours ----

/// The rage colour, normal (`+0x4148`) and raging (`+0x4144`).
inline constexpr graphics::Rgba kRageRed{170, 43, 43, 255};
inline constexpr graphics::Rgba kRageGold{217, 158, 12, 255};
/// The banner's colour while the human has state flag `0x200000`.
inline constexpr graphics::Rgba kBannerAltColour{100, 100, 200, 160};
/// The meter's background and capacity colours.
inline constexpr graphics::Rgba kMeterBackground{80, 80, 80, 255};
inline constexpr graphics::Rgba kMeterCapacity{255, 255, 255, 255};
/// The flash and spray counters, and the handcuff and key counters.
inline constexpr graphics::Rgba kCounterGreen{99, 219, 75, 255};
inline constexpr graphics::Rgba kCounterBlue{35, 83, 188, 255};
/// The hint box.
inline constexpr graphics::Rgba kHintBoxColour{0, 0, 0, 128};
/// The instruction arrow.
inline constexpr graphics::Rgba kArrowColour{191, 191, 191, 255};
/// **Coney's stand-in:** the grey of the score's leading zeros (the page says grey, not which).
inline constexpr graphics::Rgba kScoreZeroGrey{128, 128, 128, 255};

// ---- The rage meter (`HudBar`) ----

/// Rage that fills the whole meter: the constant 0.0066667 is 1/150.
inline constexpr float kRageFullScale = 150.0F;
/// The rectangles of `part_page0` the meter draws: the body, the strips, the left and right caps.
inline constexpr std::size_t kMeterBodyRect = 54;
inline constexpr std::size_t kMeterStripRect = 55;
inline constexpr std::size_t kMeterLeftCapRect = 56;
inline constexpr std::size_t kMeterRightCapRect = 57;
/// A full meter's flash: half period (`0x005100cc`) and the threshold of f³ (`0x005100c8`).
inline constexpr std::uint32_t kRageFlashHalfPeriodMs = 200;
inline constexpr float kRageFlashThreshold = 0.2F;
/// What a full meter plays, once.
inline constexpr const char* kRageFullSound = "vags/interface/rage_indicator_02";

// ---- Score and money ----

/// The score's popup: shrinks over its last 400 ms, removed at 1,000 ms (`0x0050ea3c`, `0x0050ea40`).
inline constexpr std::uint32_t kScorePopupMs = 1000;
inline constexpr std::uint32_t kScorePopupShrinkMs = 400;
/// The money's floating ±$N line lasts 1 s.
inline constexpr std::uint32_t kMoneyPopupMs = 1000;
/// The money's clamp.
inline constexpr int kMoneyMin = -99;
inline constexpr int kMoneyMax = 9999;
/// Levels numbered from here draw no score and swap player 1's rage colours.
inline constexpr int kArcadeLevelStart = 100;

// ---- Item counters ----

/// The counted items, in slot order: flash (1), spray paint (3), handcuffs (5), skeleton keys (6).
inline constexpr std::array<int, 4> kCounterItems{1, 3, 5, 6};
/// Their icons in `part_page0`. **Coney's stand-in** for the handcuffs and keys (31 and 34): the page has not found
/// their rectangles.
inline constexpr std::array<std::size_t, 4> kCounterIcons{29, 30, 31, 34};
/// The most a counter shows.
inline constexpr int kCounterMax = 9;
/// The icon's size.
inline constexpr float kCounterIconSize = 0.04F;
/// **Coney's stand-ins** where the page gives an address but no value: the slots' `x0` (`0x00510040`) as 0, line 1
/// (`0x00510048`) on the money's line, line 2 (`0x0051004c`) one text height below, and the count's offset right of its
/// icon.
inline constexpr float kSlotX0 = 0.0F;
inline constexpr float kSlotLine1 = 0.104F;
inline constexpr float kSlotLine2 = 0.154F;
inline constexpr float kCounterTextOffset = 0.022F;

// ---- Messages ----

/// Interface sound cues.
inline constexpr int kCueMoneyCount = 0x10;
inline constexpr int kCueObjective = 0x11;
inline constexpr int kCueAnnounce = 0x14;
inline constexpr int kCueHint = 0x15;
/// The objective message's place and default time; its glyph height (the page's size 0.05).
inline constexpr GuiPoint kObjectiveMessagePlace{0.025F, 0.88F};
inline constexpr std::uint32_t kObjectiveMessageMs = 8000;
inline constexpr float kMessageTextHeight = 0.05F;
/// A scroll-in message stays this long past its time.
inline constexpr std::uint32_t kScrollInTailMs = 500;
/// A scroll-in message's wrap width with one player (`0x0050ea50`; 0.52 split).
inline constexpr float kScrollInWrapWidth = 0.7F;
/// The HUD strings of the objective headers, slot 0 and slot 1, and their `CfgHUDColor` slots.
inline constexpr std::array<std::uint32_t, 2> kObjectiveHeaderStrings{0xe5, 0xe6};
inline constexpr std::array<int, 2> kObjectiveHeaderColours{4, 6};
/// The tutorial string queued the first time a slot-1 objective is set while game hints are on.
inline constexpr std::uint32_t kFirstObjectiveHint = 0x15;

/// The built-in announcement's place with one player, and the custom one's (kind 5), centred.
inline constexpr GuiPoint kAnnouncePlace{0.02F, 0.90F};
inline constexpr GuiPoint kCentredAnnouncePlace{0.5F, 0.25F};

// ---- The action prompts (`ActionPrompt_Setup`, `0x0019ef80`) ----

/// One player's prompt: centred on x, its base y, glyph height and colour.
inline constexpr GuiPoint kPromptPlace{0.5F, 0.86F};
inline constexpr float kPromptTextHeight = 0.06F;
/// The prompt text's y in the clubhouse (`HUDEnableClubActionText(true)`, `0x00607d74`), near the top.
inline constexpr float kClubPromptY = 0.125F;

/// The text scoreboard (`HUDEnableTextProgress`) and the stopwatch. **Coney stand-ins**: the original's places (the
/// floats at `0x0050d56c`, `0x0050d574`, `0x0050d57c`) and its text layout (`0x001ccf10`) are not on the page, so the
/// rows stand down the left side and the stopwatch top centre, in the text font.
inline constexpr GuiPoint kScoreRowsPlace{0.06F, 0.22F};
inline constexpr float kScoreRowStep = 0.05F;
inline constexpr float kScoreValueOffset = 0.3F;
inline constexpr float kScoreRowHeight = 0.04F;
inline constexpr GuiPoint kStopWatchPlace{0.44F, 0.08F};
inline constexpr graphics::Rgba kPromptColour{128, 128, 128, 255};
/// The raise over a showing hint box (less the box's height), else over a scroll-in message (less its height).
inline constexpr float kPromptRaiseOverHint = 0.05F;
inline constexpr float kPromptRaiseOverMessage = -0.02F;
/// A prompt of more than one line moves up this much per line.
inline constexpr float kPromptLineRaise = 0.025F;
/// The cycle icon's size (`HUDTurnOnActionCycleAnim` passes 0.1, an overlay height).
inline constexpr float kCycleIconSize = 0.1F;

// ---- The hint box (`0x0050eb50`) ----

/// The text's x and the bottom it sits on, one player.
inline constexpr float kHintTextX = 0.016F;
inline constexpr float kHintBottom = 1.0F;
/// The text widget's wrap width with one player (its `+0x1d4`, read at runtime; 0.52 split). The box layout's own
/// 0.73 (`+0x28`, docs/research/hud.md) matches the measured box; the text wraps at the widget's own width.
inline constexpr float kHintWrapWidth = 0.74F;
/// The box's offset from the text's top-left, and its size beyond the text's.
inline constexpr GuiPoint kHintBoxOffset{-0.018F, -0.035F};
inline constexpr GuiSize kHintBoxExtra{0.03F, 0.035F};
/// The hint queue's pool: a hint queued when it is full is dropped.
inline constexpr std::size_t kHintQueueSlots = 20;
/// The hint text's colour (`0x005fd310`).
inline constexpr graphics::Rgba kHintTextColour{178, 178, 178, 255};
/// The box's alpha as drawn: 125 x the text's alpha / 255 (`0x001ce8b8`).
inline constexpr std::uint8_t kHintBoxDrawAlpha = 125;
/// A `<DISPLAYTIME>` text fades over its last second.
inline constexpr std::uint32_t kHintDisplayFadeMs = 1000;
/// **Coney's stand-in** for a `<FREEZE>` hint while the game timer does not freeze: the freeze's 2 s minimum.
inline constexpr std::uint32_t kHintFreezeMinMs = 2000;
/// Hint priorities: 0 action objects, 2 the game's own hints, 1-3 scripts; flushing 4 removes all.
inline constexpr int kActionHintPriority = 0;
inline constexpr int kGameHintPriority = 2;
inline constexpr int kHintFlushAll = 4;

// ---- Counter panels ----

/// How many counter panels exist.
inline constexpr std::size_t kCounterPanels = 5;
/// Their column and rows: x 0.96, y 0.26 + 0.08 × row.
inline constexpr float kCounterPanelX = 0.96F;
inline constexpr float kCounterPanelTop = 0.26F;
inline constexpr float kCounterPanelRow = 0.08F;
/// The scripted bars move down this far while the stopwatch shows (HUD `+0x6858`).
inline constexpr float kStopWatchBarDrop = 0.07F;
/// What a panel shows for when HUDSetPHValue gives no time.
inline constexpr std::uint32_t kCounterPanelDefaultMs = 3000;
/// The default bar width of a bar panel.
inline constexpr float kCounterPanelBarWidth = 0.22F;
/// The sound a change can play.
inline constexpr const char* kCounterPanelSound = "vags/interface/menu/bonuspart_01";

// ---- The instruction arrow ----

/// Rectangle 10 of `hud_minigames`, size 0.06.
inline constexpr std::size_t kArrowRect = 10;
inline constexpr float kArrowSize = 0.06F;
/// The bob: the step runs up by 2 a frame to 10 and back by 0.5; the offset is step / 20 / 10 along the arrow.
inline constexpr float kArrowStepUp = 2.0F;
inline constexpr float kArrowStepDown = 0.5F;
inline constexpr float kArrowStepMax = 10.0F;
inline constexpr float kArrowBobDivisor = 20.0F * 10.0F;

// ---- The radar ----

/// The radar's centre in overlay-camera space at depth 1.0, one player (`0x0050d428`, `0x0050d430`).
inline constexpr float kRadarX = 0.51F;
inline constexpr float kRadarY = -0.31F;
inline constexpr float kRadarDepth = 1.0F;

} // namespace coney::hud
