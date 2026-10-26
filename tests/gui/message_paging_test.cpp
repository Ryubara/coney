// SPDX-License-Identifier: GPL-3.0-or-later
// The markup widget's word wrap, page clip and line window (docs/research/gui.md#fn-messagehud), measured with a
// made-up width of one unit a character so each break can be counted by hand.
#include "gui/message_paging.h"

#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "gui/text_layout.h"
#include "support/font_fixtures.h"

using coney::gui::clipToPage;
using coney::gui::MessagePage;
using coney::gui::pageDown;
using coney::gui::pageUp;
using coney::gui::wrapMessage;
using coney::gui::WrappedMessage;

namespace {

// One unit a character: a line's width is its length.
float charCount(std::string_view line) { return static_cast<float>(line.size()); }

} // namespace

TEST_CASE("word wrap: a CRM before the word that takes the line past the width, each word followed by a space",
          "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, confirmed (code)
    const WrappedMessage wrapped = wrapMessage("aaa bbb  cc dddd", 7.0F, charCount);
    // "aaa bbb" is 7 and fits; "aaa bbb cc" does not; "cc dddd" is 7 again. Runs of spaces are one.
    CHECK(wrapped.text == "aaa bbb <CRM>cc dddd ");
    CHECK(wrapped.breaks == 1);
    // A width of exactly the line keeps it on one line.
    CHECK(wrapMessage("ab cd", 5.0F, charCount).breaks == 0);
    CHECK(wrapMessage("ab cd", 4.9F, charCount).text == "ab <CRM>cd ");
}

TEST_CASE("word wrap: a break looks back for the last space or tag, so a one-word line takes the CRM before it along",
          "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, confirmed (code)
    // After "aaa bbb <CRM>cc ", "cc dddddd" passes 7. Looking back from "cc" for a space or a `<` finds the `<` of
    // the last `<CRM>` first, so "<CRM>cc " moves after the new `<CRM>`: an empty line, and "cc dddddd" stays one
    // line.
    const WrappedMessage wrapped = wrapMessage("aaa bbb cc dddddd", 7.0F, charCount);
    CHECK(wrapped.text == "aaa bbb <CRM><CRM>cc dddddd ");
    CHECK(wrapped.breaks == 2);
}

TEST_CASE("word wrap: tags take no width, and a tag the last word started moves to the next line",
          "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, confirmed (code)
    // `<COLOR ff>` splits into "<COLOR" and "ff>bb": neither the name nor the argument is measured. The line
    // "aa bb" (5) passes 4, so the break goes before the tag's first word, which moves with "ff>bb".
    const WrappedMessage wrapped = wrapMessage("aa <COLOR ff>bb", 4.0F, charCount);
    CHECK(wrapped.text == "aa <CRM><COLOR ff>bb ");
    CHECK(wrapped.breaks == 1);
    // A leading `<AUTOINDENT 9>` is not measured either, but the space after its first word is: the first line is
    // " aaaa bbbb", 10.
    CHECK(wrapMessage("<AUTOINDENT 9>aaaa bbbb", 10.0F, charCount).breaks == 0);
    CHECK(wrapMessage("<AUTOINDENT 9>aaaa bbbb", 9.0F, charCount).breaks == 1);
}

TEST_CASE("word wrap: a word with a tag in it is measured only from the tag on", "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, confirmed (code)
    // "xxxxxx<CENTER>" has a `>`, so it is read as starting inside a tag: its "xxxxxx" takes no width and the line
    // stays "aa" (2).
    CHECK(wrapMessage("aa xxxxxx<CENTER>", 4.0F, charCount).breaks == 0);
}

TEST_CASE("word wrap: CR and CR3 count as line breaks and start a new line; CRM and CR2 in the text do not",
          "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, confirmed (code)
    // "bb<CR>cccc" counts a break and leaves the line "cccc" ("bb", before the tag in the word, is not measured);
    // "cccc dd" passes 6, and the look back for a space or a `<` finds the `<` of `<CR>`, so the `<CRM>` goes
    // before it.
    const WrappedMessage wrapped = wrapMessage("aaaa bb<CR>cccc dd", 6.0F, charCount);
    CHECK(wrapped.text == "aaaa bb<CRM><CR>cccc dd ");
    CHECK(wrapped.breaks == 2);
    CHECK(wrapMessage("a<CR3 0.1>b", 9.0F, charCount).breaks == 1);
    CHECK(wrapMessage("a<CRM>b<CR2>c", 9.0F, charCount).breaks == 0);
}

TEST_CASE("word wrap: a word wider than the width stays alone on its line", "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, speculative (Coney's choice: the original never finishes such a text)
    const WrappedMessage wrapped = wrapMessage("aa bbbbbbbb", 4.0F, charCount);
    CHECK(wrapped.text == "aa <CRM>bbbbbbbb ");
    CHECK(wrapped.breaks == 1);
    CHECK(wrapMessage("bbbbbbbb", 4.0F, charCount).breaks == 0);
}

TEST_CASE("page clip: the wrapped lines first to last, the break after the last kept", "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, confirmed (code)
    const std::string text = "L0 <CRM>L1 <CRM>L2 <CRM>L3 ";
    CHECK(clipToPage(text, MessagePage{.firstLine = 0, .lastLine = 1}) == "L0 <CRM>L1 <CRM>");
    CHECK(clipToPage(text, MessagePage{.firstLine = 1, .lastLine = 2}) == "L1 <CRM>L2 <CRM>");
    CHECK(clipToPage(text, MessagePage{.firstLine = 2, .lastLine = 7}) == "L2 <CRM>L3 ");
    CHECK(clipToPage(text, MessagePage{.firstLine = 0, .lastLine = 5}) == text);
    // Only `<CRM>` splits: a `<CR>` line stays in its page line.
    CHECK(clipToPage("A<CRM>B<CR>C<CRM>D", MessagePage{.firstLine = 1, .lastLine = 1}) == "B<CR>C<CRM>");
    // Coney's choice: a window past the text keeps nothing (the original copies from no text).
    CHECK(clipToPage(text, MessagePage{.firstLine = 5, .lastLine = 6}).empty());
}

TEST_CASE("line window: up while the first line is above 0, down while the last is above the break count",
          "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, confirmed (code)
    MessagePage page{.firstLine = 0, .lastLine = 5};
    CHECK_FALSE(pageUp(page));
    CHECK(pageDown(page, 7));
    CHECK(pageDown(page, 7));
    CHECK_FALSE(pageDown(page, 7));
    CHECK(page.firstLine == 2);
    CHECK(page.lastLine == 7);
    CHECK(pageUp(page));
    CHECK(page.firstLine == 1);
    CHECK(page.lastLine == 6);
    MessagePage shortText{.firstLine = 0, .lastLine = 5};
    CHECK_FALSE(pageDown(shortText, 0));
}

TEST_CASE("line measure: a line's width as the layout gives it, on one line", "[gui][message_paging]") {
    // Expected: gui.md#fn-messagehud, inferred
    const coney::graphics::Font font = coney::test::testFont();
    const coney::gui::FontLookup fonts = [&font](int /*slot*/) { return &font; };
    coney::gui::TextStyle style;
    style.wrapWidth = 0.01F;
    const coney::gui::LineMeasure measure = coney::gui::lineMeasure(style, fonts);
    style.wrapWidth = 0.0F;
    CHECK(measure("AB AB") == coney::gui::layoutText("AB AB", style, fonts).width);
    CHECK(measure("") == 0.0F);
}
