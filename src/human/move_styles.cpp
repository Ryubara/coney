// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the move style tests to compile; the port follows.
#include "human/move_styles.h"

#include "human/human_animator.h"

namespace coney::human {

std::uint8_t MoveStyles::push(std::uint8_t style) { return style; }

std::uint8_t MoveStyles::pop() { return top(); }

std::uint8_t MoveStyles::replaceTop(std::uint8_t style) { return style; }

int MoveStyles::remove(std::uint8_t /*style*/) { return -1; }

int MoveStyles::carry(std::uint8_t /*style*/) { return -1; }

AnimSlots styledSlots(const AnimSlots& defaults, int /*style*/, bool /*player*/) { return defaults; }

} // namespace coney::human
