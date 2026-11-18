// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/line_editor.h"

namespace coney::console {

void LineEditor::setText(std::string_view text) { m_text = text; }

bool LineEditor::complete() { return false; }

void LineEditor::previous() {}

void LineEditor::next() {}

Outcome LineEditor::submit(PlayerSlot /*slot*/) { return Outcome::Ignored; }

} // namespace coney::console
