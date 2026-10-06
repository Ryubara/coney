// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gui/text_layout.h"
#include "hud/hud_audio.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// One message of the scroll-in queue: its text, where it shows, for how long, and the cue it plays.
struct ScrollInMessage {
    std::string text;
    GuiPoint place;
    std::uint32_t ms = kObjectiveMessageMs;
    std::optional<int> cue; ///< An interface cue to play when it shows.
};

/// The queued messages (`ScrollInHUD.cpp`, HUD `+0x8dd0`): the objectives' messages. The front message shows at its
/// place, its text block centred on the place's y, and is removed once its time has passed plus kScrollInTailMs.
///
/// Coney's choices: a message's cue plays when it comes to the front; it fades out over its tail; the original's
/// y offset (`0x0050ea58`) is taken as 0 (not on the page).
///
/// Research: docs/research/hud.md#objectives-hudsetobjective
class ScrollInQueue {
  public:
    /// Appends `message`.
    /// @orig 0x001c8b08 ScrollIn_Queue (ScrollInHUD.cpp)
    void queue(ScrollInMessage message);
    /// Removes every message.
    void clear() { m_queue.clear(); }
    /// One frame at `nowMs`: starts the front message (its cue), removes it when its time and tail have passed.
    /// @orig 0x001c9028 ScrollIn_Update (ScrollInHUD.cpp)
    void update(std::uint64_t nowMs, const HudSound& audio);
    /// Whether a message shows.
    [[nodiscard]] bool showing() const { return !m_queue.empty() && m_startMs.has_value(); }
    /// The messages, the showing one first.
    [[nodiscard]] const std::deque<ScrollInMessage>& messages() const { return m_queue; }
    /// The front message's alpha.
    [[nodiscard]] float fade() const;
    /// The showing message's height laid out with `fonts`, or 0 when none shows.
    [[nodiscard]] float showingHeight(const gui::FontLookup& fonts) const;
    /// Draws the front message.
    void render(const HudCanvas& canvas) const;

  private:
    std::deque<ScrollInMessage> m_queue;
    std::optional<std::uint64_t> m_startMs; // when the front message started
    std::uint64_t m_nowMs = 0;
};

/// One line of the objectives' checklist.
struct ChecklistLine {
    std::string text;
    bool marked = false; ///< Mode 2 marks it (inferred: ticks it off).
};

/// The objectives' checklist (`ChecklistMessageHUD.cpp`, `0x0062e790`): two slots and a third list for slot 2. Where it
/// is shown is not traced (inferred: the pause menu), so Coney keeps it for the debug menus only.
///
/// Research: docs/research/hud.md#objectives-hudsetobjective
struct Checklist {
    std::array<std::optional<ChecklistLine>, 3> slots;
};

/// An announcement (`HUDSetAnnounceMsg`): the text, when it started and when its `<DISPLAYTIME>` ends it (none: it
/// stays until replaced).
struct Announcement {
    std::string text;
    std::uint64_t startMs = 0;
    std::optional<std::uint32_t> displayMs;
    bool flag = false; ///< The binding's third argument (`0x00617fe8`), meaning not traced.
};

/// The messages' text style at GUI x `x`: the objective message's size, with a drop shadow.
[[nodiscard]] gui::TextStyle messageStyle(float x);
/// Lays out `text` in `style` with its block centred on GUI y `centreY` and adds its sprites.
void drawMessage(const HudCanvas& canvas, std::string_view text, gui::TextStyle style, float centreY);

} // namespace coney::hud
