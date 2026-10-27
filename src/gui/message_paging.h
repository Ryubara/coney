// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "gui/text_layout.h"

namespace coney::gui {

/// The width of one line of text with its tags already stripped, GUI units: what the word wrap measures with.
using LineMeasure = std::function<float(std::string_view line)>;

/// A text after the markup widget's word wrap.
struct WrappedMessage {
    std::string text;       ///< The words, each followed by one space, with a `<CRM>` at each break the wrap made.
    std::size_t breaks = 0; ///< The line breaks counted (the widget's `+0x1ac`): the wrap's and the text's own.
};

/// The markup widget's line window (`+0x1a4` and `+0x1a8`): the wrapped lines firstLine to lastLine, both shown.
struct MessagePage {
    std::size_t firstLine = 0;
    std::size_t lastLine = 0;
};

/// Word-wraps marked-up `text` at `wrapWidth` (the widget's `+0x1d4`), as the markup widget does before each draw.
/// The text is split at spaces (runs of spaces are one, and every word is followed by one space in the result); each
/// word joins the current line, measured by `measure` without its tags, and a `<CRM>` goes before a word that takes
/// the line past the width. The original's quirks are kept:
///
/// - a word with a `>` in it is read as starting inside a tag, so only what follows its first `>` is measured (the
///   argument of a tag split by a space, but also a word before a tag in the same word);
/// - the space after a word that adds nothing to the line (a tag) is measured at the start of the next;
/// - each `<` in a word that holds `<CR>` or `<CR3` counts a break and starts a new line; `<CRM>` and `<CR2>` in the
///   text count nothing;
/// - the `<CRM>` goes before the last space or `<` of the result so far: after the last word, or before a tag that
///   ends it (a tag's name, whose argument starts the word that did not fit), or before the last `<CRM>` when the
///   current line holds one word, which then leaves an empty line and keeps that word with the next.
///
/// Coney's choice: a word wider than the width on a line of its own stays there (the original never finishes such a
/// text).
///
/// Research: docs/research/pause.md#stats-description
/// @orig 0x001bac20 MessageHUD_WordWrap (unknown)
[[nodiscard]] WrappedMessage wrapMessage(std::string_view text, float wrapWidth, const LineMeasure& measure);

/// The wrapped lines of `page`, split at `<CRM>`: from the start of line firstLine to the `<CRM>` that ends line
/// lastLine, that `<CRM>` included, or to the end of the text. Coney's choice: nothing when the text has fewer
/// lines than firstLine (the original copies from no text).
///
/// Research: docs/research/pause.md#stats-description
/// @orig 0x001baee0 MessageHUD_ClipToPage (unknown)
[[nodiscard]] std::string clipToPage(std::string_view wrapped, const MessagePage& page);

/// Moves the window up one line while its first line is above 0; returns whether it moved.
/// @orig 0x001bafd0 MessageHUD_PageUp (unknown)
bool pageUp(MessagePage& page);

/// Moves the window down one line while its last line is below `breaks` (the wrap's count: the text's last line);
/// returns whether it moved.
/// @orig 0x001bb000 MessageHUD_PageDown (unknown)
bool pageDown(MessagePage& page, std::size_t breaks);

/// The word wrap's measure of a line in `style` with `fonts`: its width laid out on one line, unwrapped (the
/// original measures it with Font_Measure in the widget's font, scale and proportional flag).
/// @coney-only Coney measures through its layout; the original calls Font_Measure
[[nodiscard]] LineMeasure lineMeasure(const TextStyle& style, const FontLookup& fonts);

} // namespace coney::gui
