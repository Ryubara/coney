// SPDX-License-Identifier: GPL-3.0-or-later
// The move style stack and each style's anim slots (docs/research/objects.md#two-handed,
// docs/research/characters.md#anim-slots).
#include "human/move_styles.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "human/human_animator.h"

namespace coney::human {

namespace {

// One slot a style writes and the anim id it writes there.
using SlotId = std::pair<std::size_t, std::uint32_t>;

// The player's combat walk, which styles 1-3 put in slot 14 for a player.
constexpr std::uint32_t kPlayerCombatWalk = 380;

// Writes each of `ids` into `slots`.
// @coney-only a helper of styledSlots(), which ports Human_ApplyMoveStyle
void write(AnimSlots& slots, std::initializer_list<SlotId> ids) {
    for (const auto& [slot, id] : ids) {
        slots.ids.at(slot) = id;
    }
}

// Styles 1-3 (knife, baton, bat) end alike: the strike in `slot` and the four block slots.
// @coney-only a helper of styledSlots(), the switch's shared tail of cases 2 and 3
void armedBlocks(AnimSlots& slots, std::size_t slot, std::uint32_t id) {
    write(slots, {{slot, id}, {21, 621}, {23, 622}, {24, 623}, {22, 624}});
}

} // namespace

std::uint8_t MoveStyles::push(std::uint8_t style) {
    // A full stack keeps its top two pushed styles and loses the lowest.
    if (depth < kMaxDepth) {
        ++depth;
    } else {
        entries[1] = entries[2];
        entries[2] = entries[3];
    }
    entries.at(static_cast<std::size_t>(depth)) = style;
    return style;
}

std::uint8_t MoveStyles::pop() {
    if (depth > 0) {
        entries.at(static_cast<std::size_t>(depth)) = kStyleNone;
        --depth;
    }
    return top();
}

std::uint8_t MoveStyles::replaceTop(std::uint8_t style) {
    if (depth == 0 && style == kStyleNone) {
        return kStyleNone;
    }
    static_cast<void>(pop());
    return push(style);
}

int MoveStyles::remove(std::uint8_t style) {
    // The lowest entry holding it, searched from the base; past the top when it is not there.
    int at = 0;
    if (depth >= 0 && entries[0] != style) {
        at = 1;
        while (at <= depth && entries.at(static_cast<std::size_t>(at)) != style) {
            ++at;
        }
    }
    // The top: popped, and the style beneath applied.
    if (at == depth) {
        return pop();
    }
    // Under the top: the entries above move down over it; the top is unchanged, so nothing is applied.
    if (at < depth) {
        for (int i = at; i <= depth - 1; ++i) {
            entries.at(static_cast<std::size_t>(i)) = entries.at(static_cast<std::size_t>(i) + 1);
        }
        entries.at(static_cast<std::size_t>(depth)) = kStyleNone;
        --depth;
    }
    return -1;
}

int MoveStyles::carry(std::uint8_t style) {
    if (style == kStyleNone) {
        return -1;
    }
    const std::uint8_t current = top();
    // Hidden, a carried set 4 or 6 takes the hidden style's place.
    if (current == kStyleHidden && (style == kStyleGhetto || style == kStyleCarry)) {
        return replaceTop(style);
    }
    if (current == kStyleNone) {
        return push(style);
    }
    // Another style on top stays on top, the carried one going under it.
    static_cast<void>(pop());
    static_cast<void>(push(style));
    return push(current);
}

AnimSlots styledSlots(const AnimSlots& defaults, int style, bool player) {
    // The slots reset to the defaults, then the style's ids written over them.
    AnimSlots slots = defaults;
    switch (style) {
    case 1:
        write(slots, {{16, 45}, {17, 47}, {18, 49}, {20, 50}, {19, 48}});
        if (player) {
            write(slots, {{14, kPlayerCombatWalk}});
        }
        break;
    case 2:
        if (player) {
            write(slots, {{14, kPlayerCombatWalk}});
        }
        write(slots, {{16, 39}, {17, 41}, {18, 43}, {20, 44}});
        armedBlocks(slots, 19, 42);
        break;
    case 3:
        if (player) {
            write(slots, {{14, kPlayerCombatWalk}});
        }
        write(slots, {{16, 34}, {17, 36}, {19, 37}});
        armedBlocks(slots, 18, 38);
        break;
    case kStyleCarry:
        // The barrel clips: idle, walk starts, run stop, walks, the fight steps and turns.
        write(slots, {{0, 509},
                      {9, 519},
                      {10, 519},
                      {33, 523},
                      {3, 515},
                      {4, 515},
                      {5, 516},
                      {6, 517},
                      {7, 518},
                      {1, 535},
                      {2, 537},
                      {8, 529},
                      {11, 527},
                      {12, 535},
                      {13, 537},
                      {14, 541},
                      {15, 529}});
        break;
    case kStyleGhetto:
        // The same shape with the ghetto clips.
        write(slots, {{0, 555},
                      {9, 565},
                      {10, 565},
                      {33, 569},
                      {3, 561},
                      {4, 561},
                      {5, 562},
                      {6, 563},
                      {7, 564},
                      {1, 581},
                      {2, 583},
                      {8, 575},
                      {11, 573},
                      {12, 581},
                      {13, 583},
                      {14, 587},
                      {15, 575}});
        break;
    case 7:
        write(slots, {{0, 82}, {11, 82}, {16, 51}, {17, 55}, {4, 86}, {5, 86}, {6, 86}, {7, 86}});
        break;
    case 8:
        write(slots, {{0, 83}, {11, 83}, {4, 87}, {5, 87}, {6, 87}});
        break;
    case 9:
        write(slots, {{0, 84}, {11, 84}, {4, 88}, {5, 88}, {6, 88}});
        break;
    case 10:
        write(slots, {{0, 85}, {11, 85}, {4, 89}, {5, 89}, {6, 89}});
        break;
    case 11:
        write(slots, {{0, 207}, {11, 207}});
        break;
    case 12:
        write(slots, {{0, 210}, {11, 210}, {16, 219}, {17, 223}});
        break;
    case 13:
        write(slots, {{0, 200}, {11, 200}, {4, 201}, {14, 201}});
        break;
    case 14:
        write(slots, {{0, 356}, {11, 356}});
        break;
    case 15:
    case 16:
        write(slots, {{0, 196}, {11, 196}});
        break;
    case 17:
        write(slots, {{0, 320}, {11, 320}});
        break;
    case 18:
        write(slots, {{0, 349}, {11, 349}});
        break;
    case 19:
        write(slots, {{21, 466}, {23, 469}, {24, 470}, {0, 469}, {11, 469}});
        break;
    case kStyleHidden:
        // The stealth idle, walk and walk start.
        write(slots, {{11, 630}, {0, 630}, {4, 633}, {3, 633}, {10, 631}, {9, 631}});
        break;
    default:
        break;
    }
    return slots;
}

} // namespace coney::human
