// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gui/text_layout.h"
#include "hud/hud_audio.h"
#include "hud/hud_canvas.h"

namespace coney::hud {

/// One queued hint: its text and priority (0-3, a lower number is more urgent).
struct Hint {
    std::string text;
    int priority = 1;
};

/// The times a text's markup sets: `<DISPLAYTIME ms>` and `<FREEZE ms>`, each the first one found.
struct MarkupTimes {
    std::optional<std::uint32_t> displayMs;
    std::optional<std::uint32_t> freezeMs;
};

/// Reads the `<DISPLAYTIME>` and `<FREEZE>` tags of marked-up `text`; a tag whose argument is not a number is skipped.
[[nodiscard]] MarkupTimes markupTimesOf(std::string_view text);

/// Breaks marked-up `text` into lines no wider than `width` (GUI) laid out in `style` with `fonts`, as the original's
/// word wrap does (docs/research/gui.md, `MessageHUD_WordWrap`): at spaces outside tags, greedily, a break before the
/// word that takes the line past the width, each line measured with its tags stripped (so a button icon takes no
/// width); a leading `<AUTOINDENT f>` makes `f` the width. Coney inserts `<CR>` where the original inserts `<CRM>`
/// (the same break here); a word wider than the width stays on its own line.
/// @orig 0x001bac20 MessageHUD_WordWrap (unknown)
[[nodiscard]] std::string wrapText(std::string_view text, const gui::TextStyle& style, const gui::FontLookup& fonts,
                                   float width);

/// The hint box at the bottom left (`TutorialHUD.cpp`, HUD `+0x8a10`): one hint at a time from a queue of at most
/// kHintQueueSlots, sorted by priority (lowest number first, first in first out within one). A hint queued while the
/// box is free shows at once, in the same call, with interface cue 0x15; so does the next one when the showing hint is
/// withdrawn. A hint lasts only as its markup says: `<DISPLAYTIME ms>` ends it `ms` after it showed (fading over the
/// last second) and the next one shows on the following step; with neither tag it stays until cleared, flushed or
/// interrupted. `HUDSetTutorialText` queues (nil clears), `HUDFlushTutorialText` removes by priority,
/// `HUDCheckTutorialText` asks.
///
/// Coney's stand-in: the game timer does not freeze yet, so a `<FREEZE ms>` hint shows for the larger of `ms` and
/// kHintFreezeMinMs. The text is copied, not kept by pointer.
///
/// Research: docs/research/hud-messages.md#hints-hudsettutorialtext, docs/research/hud-messages.md#hint-queue-runtime
class HintBox {
  public:
    /// A box whose cue plays through `sound` (silent when it is empty).
    explicit HintBox(HudSound sound = {}) : m_sound(std::move(sound)) {}

    /// `HUDSetTutorialText(text, priority)`: inserts `text` by priority, or drops it when the queue is full; there is
    /// no duplicate check, so the same text twice is two entries. A hint more urgent than the showing one interrupts
    /// it: the showing hint goes back in the queue to start again later. Then, when nothing shows, the front of the
    /// queue shows at once: a script's nil then text shows the text in the update the script ran.
    /// @orig 0x001ce3c0 HintBox_Queue (TutorialHUD.cpp)
    void queue(std::string text, int priority);
    /// `HUDSetTutorialText(nil)`: removes the showing hint; the next one waits for the next update().
    void clearShowing() { m_showing.reset(); }
    /// `HUDFlushTutorialText(priority)`: removes the queued and showing hints of `priority`, or every hint for
    /// kHintFlushAll; the next one waits for the next update().
    /// @orig 0x001ce748 HintBox_FlushPriority (TutorialHUD.cpp)
    void flush(int priority);
    /// `HUDCheckTutorialText(text)`: whether `text` is showing or queued.
    /// @orig 0x001ce550 HintBox_Contains (TutorialHUD.cpp)
    [[nodiscard]] bool contains(std::string_view text) const;
    /// Removes one hint by its text: the showing one, after which the next shows at once, or else the first queued
    /// one with that text.
    /// @orig 0x001ce5c0 HintBox_Withdraw (TutorialHUD.cpp)
    void withdraw(std::string_view text);

    /// One step at game time `nowMs`: a showing hint whose markup time is over is cleared and the next one waits for
    /// the following step; with none showing, the front of the queue shows, with its cue. A hidden hint's time runs
    /// on.
    /// @orig 0x001cdc80 HintBox_Update (TutorialHUD.cpp)
    void update(std::uint64_t nowMs);

    /// The hint showing, if any.
    [[nodiscard]] const std::optional<Hint>& showing() const { return m_showing; }
    /// The hints waiting, the next first.
    [[nodiscard]] const std::deque<Hint>& queued() const { return m_queue; }
    /// The showing hint's alpha: 1, falling to 0 over the last second of its `<DISPLAYTIME>`.
    [[nodiscard]] float fade() const;
    /// How long the showing hint has shown.
    [[nodiscard]] std::uint64_t shownMs() const { return m_showing ? m_nowMs - m_shownAtMs : 0; }

    /// The style the hint text is laid out in (before its y is placed).
    [[nodiscard]] static gui::TextStyle textStyle();
    /// The box's height for the showing hint laid out with `fonts`, or 0 when none shows.
    [[nodiscard]] float boxHeight(const gui::FontLookup& fonts) const;
    /// Draws the showing hint: the box (into the canvas's flat batch), then the wrapped text with its bottom at
    /// kHintBottom less the box's extra height, at kHintTextX.
    /// @orig 0x001ce8b8 HintBox_Draw (TutorialHUD.cpp)
    void render(const HudCanvas& canvas) const;

  private:
    // Inserts `hint` before the first queued hint with a higher number.
    void insert(Hint hint);
    // Shows the front of the queue, if any, with its cue and its `hint` event.
    void takeNext();
    // The showing hint wrapped to the box (cached by its source text), and its laid-out size.
    [[nodiscard]] const std::string& wrapped(const gui::TextStyle& style, const gui::FontLookup& fonts) const;

    HudSound m_sound;
    std::deque<Hint> m_queue;
    std::optional<Hint> m_showing;
    MarkupTimes m_times; // the showing hint's
    std::uint64_t m_shownAtMs = 0;
    std::uint64_t m_nowMs = 0;
    mutable std::string m_wrappedFrom;
    mutable std::string m_wrapped;
};

} // namespace coney::hud
