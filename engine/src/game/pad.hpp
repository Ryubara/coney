#pragma once

// A player's controller as the game sees it: the button word, the two sticks and the commands its bindings give
// (research/pages/pad-input/page.md). Nothing here knows about keyboards or gamepads; src/input/ fills these in.

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <span>

namespace coney::game {

// One bit per button, set while it is down (Button word).
using ButtonWord = std::uint16_t;

namespace button {
inline constexpr ButtonWord kL2 = 0x0001;
inline constexpr ButtonWord kR2 = 0x0002;
inline constexpr ButtonWord kL1 = 0x0004;
inline constexpr ButtonWord kR1 = 0x0008;
inline constexpr ButtonWord kTriangle = 0x0010;
inline constexpr ButtonWord kCircle = 0x0020;
inline constexpr ButtonWord kCross = 0x0040;
inline constexpr ButtonWord kSquare = 0x0080;
inline constexpr ButtonWord kSelect = 0x0100;
inline constexpr ButtonWord kL3 = 0x0200;
inline constexpr ButtonWord kR3 = 0x0400;
inline constexpr ButtonWord kStart = 0x0800;
inline constexpr ButtonWord kDpadUp = 0x1000;
inline constexpr ButtonWord kDpadRight = 0x2000;
inline constexpr ButtonWord kDpadDown = 0x4000;
inline constexpr ButtonWord kDpadLeft = 0x8000;
inline constexpr ButtonWord kDpad = kDpadUp | kDpadRight | kDpadDown | kDpadLeft;
} // namespace button

inline constexpr int kButtonCount = 16;
// Players 1 and 2, one controller each (Ports and controllers).
inline constexpr int kPlayerCount = 2;

// --- Sticks (Analogue sticks; Right stick and camera) ---

// A stick as the device reports it: one byte per axis, 0 to 255, centre 128; x grows to the right, y downwards.
inline constexpr std::uint8_t kStickCentre = 128;
struct StickBytes {
    std::uint8_t x = kStickCentre;
    std::uint8_t y = kStickCentre;
};

// The table's dead zone, per axis: bytes in it give 0.
inline constexpr std::uint8_t kDeadZoneLow = 95;
inline constexpr std::uint8_t kDeadZoneHigh = 160;
// The right stick's own, wider dead zone, per axis.
inline constexpr std::uint8_t kRightDeadZoneXLow = 65;
inline constexpr std::uint8_t kRightDeadZoneXHigh = 175;
inline constexpr std::uint8_t kRightDeadZoneYLow = 9;
inline constexpr std::uint8_t kRightDeadZoneYHigh = 231;

struct StickValue {
    // From -1 to 1 per axis; right and up are positive.
    float x = 0.0f;
    float y = 0.0f;
    // The unit vector (x, y) points along, or zero when the stick is centred.
    float direction_x = 0.0f;
    float direction_y = 0.0f;
    // The length of (x, y), capped at 1.
    float length = 0.0f;
};

struct PadSticks {
    StickBytes left;
    StickBytes right;
};

struct PlayerSticks {
    StickValue left;
    StickValue right;
};

// One axis byte through the stick table.
[[nodiscard]] float stick_axis_value(std::uint8_t byte) noexcept;

// Both sticks of one player's controller as the game keeps them.
[[nodiscard]] PlayerSticks convert_sticks(const PadSticks& sticks) noexcept;

// --- Commands (Trigger kinds) ---

// How a binding's buttons must move; the numbers are the page's kinds.
enum class TriggerKind : std::uint8_t {
    Held = 1,
    Pressed = 2,
    Released = 3,
    Repeat = 4,
    Tapped = 5,
    LongHold = 6,
    HistoryHold = 7,
    CombinationHeld = 8,
    CombinationPress = 9,
};

// When several kinds match on one update the lowest rank wins. Indexed by the kind's number.
inline constexpr std::array<int, 10> kTriggerRank{0, 9, 7, 1, 6, 3, 2, 8, 5, 4};

// Update counts.
inline constexpr int kHistoryLength = 8;     // button words kept per player
inline constexpr int kTapMostUpdates = 6;    // tapped: up after 1 to this many updates down
inline constexpr int kLongHoldUpdates = 4;   // long hold: up before this many updates down, or reaching it
inline constexpr int kHistoryHoldLength = 5; // history hold: down this many updates, up to the history's length
inline constexpr int kRepeatFirst = 15;      // a held d-pad direction fires again on this held update
inline constexpr int kRepeatEvery = 4;       // and every this many held updates after it

// Command ids run from 1 to this.
inline constexpr int kMostCommandId = 57;

// A command bound to buttons. The simple kinds use `buttons`; a group of several buttons counts as down when all
// of them are. The combination kinds use both groups: `buttons` first, `second` second.
struct CommandBinding {
    int command = 0;
    TriggerKind kind = TriggerKind::Held;
    ButtonWord buttons = 0;
    ButtonWord second = 0;
};

// What one player's controller has done so far.
struct PadHistory {
    // The button words of the last updates, after the d-pad rule; [0] is the latest.
    std::array<ButtonWord, kHistoryLength> words{};
    // Per button bit: how many updates in a row it has been down, counting the latest; 0 while up.
    std::array<int, kButtonCount> held_updates{};
    // Per d-pad bit, before the d-pad rule: how many updates in a row it has been down.
    std::array<int, kButtonCount> raw_held_updates{};
};

struct PlayerPad {
    PadHistory history;
    // Commands switched off for this player, by id.
    std::bitset<kMostCommandId + 1> switched_off;
};

// Adds this update's button word to the player's history and returns the command the player carries, if any.
[[nodiscard]] std::optional<int> update_pad_commands(PlayerPad& pad, ButtonWord buttons,
                                                     std::span<const CommandBinding> bindings);

} // namespace coney::game
