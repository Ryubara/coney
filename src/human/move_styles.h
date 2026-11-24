// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// A human's move styles: the small stack of styles (human record `+0x0c` depth, `+0x10` bytes) whose top decides the
// human's anim slots, and the slots each style writes over the defaults. A held object pushes its type's anim set
// (`CfgObj` `+0x87`) as a style, so a two-handed object (set 4) is carried with the barrel idle and walks; the hidden
// style `0x14` is the stealth walk.
// Research: docs/research/objects.md#two-handed, docs/research/characters.md#anim-slots,
// docs/research/stealth.md#sneaking

namespace coney::human {

struct AnimSlots;

/// The move styles Coney names: none, the two-handed carry (set 4, `OVERHEAD_WEAPON_SET`), the ghetto blasters (set 6,
/// `GHETTO_SET`) and the hidden style (`0x14`).
inline constexpr std::uint8_t kStyleNone = 0;
inline constexpr std::uint8_t kStyleCarry = 4;
inline constexpr std::uint8_t kStyleGhetto = 6;
inline constexpr std::uint8_t kStyleHidden = 0x14;

/// The stack of move styles: entry 0 is the base (style 0), entries 1-3 the pushed styles, `depth` the top's index.
/// Each change returns the style to apply to the anim slots, or nothing when the top did not change (a removal from
/// under the top).
struct MoveStyles {
    /// The deepest the stack goes: a push on a full stack drops the lowest pushed style.
    static constexpr int kMaxDepth = 3;
    std::array<std::uint8_t, kMaxDepth + 1> entries{};
    int depth = 0;

    /// The style on top (0 with none pushed).
    /// @orig 0x00254060 Human_GetMoveStyle (unknown)
    [[nodiscard]] std::uint8_t top() const { return entries.at(static_cast<std::size_t>(depth)); }
    /// Pushes `style`; on a full stack the pushed styles move down one, losing the lowest. Returns `style`, which is
    /// applied.
    /// @orig 0x00253ed0 Human_PushMoveStyle (unknown)
    std::uint8_t push(std::uint8_t style);
    /// Pops the top (nothing to pop at the base) and returns the style beneath, which is applied.
    /// @orig 0x00253f28 Human_PopMoveStyle (unknown)
    std::uint8_t pop();
    /// Replaces the top with `style` (a pop then a push), or applies style 0 when nothing is pushed and `style` is 0.
    /// Returns the style applied.
    /// @orig 0x00253e68 Human_SetMoveStyle (unknown)
    std::uint8_t replaceTop(std::uint8_t style);
    /// Removes the lowest entry holding `style`: from the top it is a pop and the style beneath is returned; from
    /// under the top the entries above it move down and nothing is returned, the top being unchanged; a style not on
    /// the stack changes nothing.
    /// @orig 0x00253f78 Human_RemoveMoveStyle (unknown)
    int remove(std::uint8_t style);
    /// The take's style change for a held object of anim set `style` (`Human_SetCarryMoveStyle`): nothing for 0; over
    /// the hidden style a set 4 or 6 replaces it; with nothing pushed it is pushed; over another style it goes under
    /// that one (popped, this pushed, the old pushed back). Returns the style applied last, or -1 for none.
    /// @orig 0x00221ed0 Human_SetCarryMoveStyle (unknown)
    int carry(std::uint8_t style);
};

/// The anim slots for `style`: `defaults` with the style's ids written over them (`Human_ApplyMoveStyle`'s cases
/// 1-20; any other style keeps the defaults). `player` is whether the human is a player (record `+0x1b0` not −1),
/// which gives styles 1-3 the player's combat walk 380 in slot 14.
/// @orig 0x00253688 Human_ApplyMoveStyle (unknown)
[[nodiscard]] AnimSlots styledSlots(const AnimSlots& defaults, int style, bool player);

} // namespace coney::human
