// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::hud {

/// The credits' layout and pacing (docs/research/frontend.md#fn-credits): the pool of text widgets the scroll reuses,
/// where a line starts (`+0x174`) and where it is given back (`+0x170`), the column's centre (`+0x178`) and the gutter
/// between a role and its name (`+0x17c`), the step every live line moves up each update (`+0x180`, `0x0050d344`), the
/// text sizes (`0x0050d348`, `0x0050d34c`) and the gaps after a line (`0x0050d354`, `0x0050d350`, `0x0050d358`).
inline constexpr std::size_t kCreditWidgets = 70;
inline constexpr float kCreditsStartY = 1.1F;
inline constexpr float kCreditsTopLimit = -0.1F;
inline constexpr float kCreditsCentreX = 0.5F;
inline constexpr float kCreditsGutter = 0.02F;
inline constexpr float kCreditsStep = 0.0025F;
inline constexpr float kCreditHeadingScale = 1.8F;
inline constexpr float kCreditLineScale = 0.9F;
inline constexpr float kCreditGapAfterHeading = 0.05F;
inline constexpr float kCreditGapBeforeHeading = 0.08F;
inline constexpr float kCreditGapAfterSharedRole = 0.04F;

/// A credit line's style (`CreditData +0x00`, `CfgCredits`' first argument).
enum class CreditStyle : std::uint8_t {
    Heading = 0,  ///< A centred heading at kCreditHeadingScale.
    RoleName = 1, ///< A role (right of the gutter's left edge, shown only when it changes) and a name.
    Line = 2,     ///< A centred line at kCreditLineScale; `CfgCredits` style 4 becomes this.
    Section = 3,  ///< A centred line like Line; a heading or role line before it leaves kCreditGapBeforeHeading.
};

/// One line of the credits (`CreditData`, 0x1c bytes): its style, its two texts, the pool widgets it holds while live
/// (`+0x0c`, `+0x10`), its index in the list (`+0x14`) and the gap left after it (`+0x18`).
struct CreditLine {
    std::uint32_t style = 0;
    std::string role; ///< The first text: a heading, a centred line's text, or a role.
    std::string name; ///< The second text: the name of a role line; empty for none.
    std::size_t index = 0;
    float gap = 0.0F;
    std::optional<std::size_t> widget;     ///< The pool widget of the main text, while live.
    std::optional<std::size_t> roleWidget; ///< A role line's role widget, when its role was shown.
    bool started = false;                  ///< Has taken widgets (Coney's flag: the original reads `+0x0c`).
    bool finished = false;                 ///< Has passed the top and given its widgets back.
};

/// The end-credits scroll (HUD `+0xea30`, class `Credits`): `PreloadCredits` fills the list from `credits.lua`'s
/// `CfgCredits` calls and makes the widget pool, `ShowCredits` starts it, and each update moves the lines up the screen
/// at a fixed step per update (not timed), starting a line at the bottom while the pool has two widgets free and giving
/// a line's widgets back once it passes the top. When no widget shows any more the scroll shuts down and reports its
/// end, for which the caller runs the Lua function `CreditEnd`.
///
/// It is updated and drawn in the overlay pass (`GameMode_DrawOverlays`), so it runs whatever the HUD's own
/// visibility: Hud::update() and Hud::render() step and draw it before their hidden or letterbox early returns.
///
/// Coney's choices where the page is silent (each an open question on docs/research/frontend.md):
/// - a heading or centred line shows its first text, or its second when the first is empty;
/// - a new role resets the shared-role count to 1;
/// - the pen walking down the live lines starts at kCreditsStartY when the first live line has not started yet, and
///   at that line's centre otherwise (so "half for the first" its height); a new line's centre is the pen plus half
///   its height, and every later line advances the pen by its whole height, then its gap;
/// - a line's height is one line of its text size (graphics::fontMetrics()); credit texts are single lines;
/// - CfgCredits before PreloadCredits adds to the list, which the load then clears.
///
/// Research: docs/research/frontend.md#fn-credits
/// @orig 0x001a90f8 Credits_Construct (Credits.cpp)
class Credits {
  public:
    Credits();

    /// `CfgCredits(style, role, name)`: appends a line (style 4 becomes 2) whose index is the list's size.
    /// @orig 0x001a8fb8 Credits_AddLine (Credits.cpp)
    void addLine(std::uint32_t style, std::string_view role, std::string_view name);

    /// `PreloadCredits`: a fresh list, then `runScript` (which runs `credits.lua`, whose `CfgCredits` calls fill it),
    /// then the pool of hidden widgets and the scroll's state (last role none, shared-role count 1); marks it loaded.
    /// @orig 0x001a9130 Credits_Load (Credits.cpp)
    /// @orig 0x001b38b8 HUD_LoadCredits (unknown)
    void load(const std::function<void()>& runScript);

    /// `ShowCredits`: when loaded and not running, starts the scroll from the first line at `nowMs`.
    /// @orig 0x001a9480 Credits_Start (Credits.cpp)
    /// @orig 0x001b38d8 HUD_StartCredits (unknown)
    void start(std::uint64_t nowMs);

    /// When running: hides every widget, drops the list and clears running and loaded.
    /// @orig 0x001a94e8 Credits_Shutdown (Credits.cpp)
    void shutdown();

    /// One update of the scroll (once per HUD step). From the first live line on, a line not yet started takes its
    /// widgets while two or more are free (else no later line starts this update), is laid out and placed below the
    /// one before; a live line is checked against the top limit and moved up by kCreditsStep. When no widget shows any
    /// more it shuts down and marks its end (takeEnded()).
    /// @orig 0x001a9e90 Credits_Update (Credits.cpp)
    void update();

    /// Adds every shown widget whose top is above kCreditsStartY, in the menus' grey (`0x005fd310`).
    /// @orig 0x001aa108 Credits_Render (Credits.cpp)
    void render(const gui::GuiCanvas& canvas) const;

    /// Whether the scroll ended since the last call (and clears it): the caller then calls the Lua `CreditEnd`.
    [[nodiscard]] bool takeEnded();

    [[nodiscard]] bool loaded() const { return m_loaded; }
    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] std::uint64_t startMs() const { return m_startMs; }
    [[nodiscard]] const std::vector<CreditLine>& lines() const { return m_lines; }
    /// The pool widget `index`, below kCreditWidgets.
    [[nodiscard]] const gui::TextWidget& widget(std::size_t index) const { return m_pool.at(index); }
    /// How many pool widgets are hidden (free).
    /// @orig 0x001a9608 Credits_CountFreeWidgets (Credits.cpp)
    [[nodiscard]] std::size_t freeWidgets() const;
    /// The line height of a credit of `style`: one line at its text size.
    [[nodiscard]] static float lineHeight(std::uint32_t style);

  private:
    // The first hidden pool widget, if any.
    std::optional<std::size_t> findFreeWidget() const;
    // Takes the line's widgets and lays them out (role only when it changed; else counts the shared role).
    void assignWidgets(CreditLine& line);
    // Moves the line's widgets so that their centre is at y.
    void placeLine(const CreditLine& line, float y);
    // The gap after line `index`, from its style, the next line's and the shared-role count.
    float gapAfter(std::size_t index) const;
    // A live line's height: the larger of its widgets' heights.
    float heightOf(const CreditLine& line) const;
    // Gives a line's widgets back (hidden) once it is above the top limit; advances the first live line.
    void updateLine(CreditLine& line);
    // Moves a live line's widgets up by kCreditsStep.
    void scrollLine(const CreditLine& line);
    // Whether any pool widget shows.
    [[nodiscard]] bool anyShown() const;

    std::array<gui::TextWidget, kCreditWidgets> m_pool;
    std::array<float, kCreditWidgets> m_heights{}; // Each widget's text height (`+0xb4`).
    std::vector<CreditLine> m_lines;
    std::size_t m_firstLive = 0; // `+0x160`.
    std::string m_lastRole;      // `+0x188`.
    bool m_hasLastRole = false;
    int m_sharedRole = 1; // `+0x18c`.
    std::uint64_t m_startMs = 0;
    bool m_loaded = false;
    bool m_running = false;
    bool m_ended = false;
};

} // namespace coney::hud
