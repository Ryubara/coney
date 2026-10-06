// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "graphics/sprite_batch.h"
#include "gui/base_widget.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/option_grid.h"
#include "gui/pause_menu/pause_menu.h"
#include "gui/pause_menu/yes_no_box.h"
#include "gui/text_widget.h"
#include "gui/usage_info.h"
#include "gui/widget.h"
#include "warriors/game_state.h"

namespace coney::gui {

/// The codes of the mission-failed menu's items (Coney's numbering: the original's are not traced).
enum class FailedItem : std::uint8_t {
    LastCheckpoint = 0,
    RestartLevel = 1,
    Quit = 2,
    ToHangout = 3,
};

/// The mission-failed menu (`0x006294c0`), which mode 0xc opens when a script calls `HUDLaunchMissionFailed(reason)`.
///
/// - **Look** (open()): the "mission failed" title sprite (rectangle 0 of sheet record titleRecord()) at (0.5, 0.38),
///   size 0.4, grey 191; the reason in the front end's red, `big_font`, centred at (0.52, 0.49) (0.38 on the first
///   frame), or string 0xda when none was given; the grid from y 0.57 in rows of 1 and 2, **Last checkpoint** (0xd7),
///   then **Restart level** (0xd8) : **Quit** (0xd9), or in `level95` one row, To Hangout (0xdf) : Quit; the usage
///   line 0x1b at (0.5, 0.68).
/// - **Quit** asks 0x102 in a Yes/No box (question at (0.08, 0.75), answers at (0.44, 0.87), No selected).
///
/// Coney's stand-ins (docs/research/pause.md#coneys-implementation): the items' actions are not traced, so accept on
/// Last checkpoint, Restart level and To Hangout ends the menu at once with the pause menu's matching outcome, and Yes
/// to Quit with PauseOutcome::QuitToMainMenu; back does nothing; the grid looks like the pause menu's.
///
/// Research: docs/research/pause.md#the-mission-failed-screen
/// @orig 0x001d23d0 MissionFailedMenu_Open (unknown)
class MissionFailedMenu : public Widget {
  public:
    /// The title sprite's sheet records: English and other languages, language 2, language 1.
    static constexpr std::uint32_t kTitleRecord = 0x20b;
    static constexpr std::uint32_t kTitleRecordLanguage2 = 0x20c;
    static constexpr std::uint32_t kTitleRecordLanguage1 = 0x20d;
    /// The title's place, size, grey and depth.
    static constexpr float kTitleY = 0.38F;
    static constexpr float kTitleSize = 0.4F;
    static constexpr std::uint8_t kTitleGrey = 191;
    static constexpr float kTitleDepth = 11000.0F;
    /// The reason's place, and its y on the first frame.
    static constexpr float kReasonX = 0.52F;
    static constexpr float kReasonY = 0.49F;
    static constexpr float kReasonFirstY = 0.38F;
    /// The grid's first row and the usage line's y.
    static constexpr float kGridY = 0.57F;
    static constexpr float kUsageY = 0.68F;
    /// The world's fade (screen effect 1) when the menu opens, in seconds.
    static constexpr double kFadeSeconds = 2.0;

    /// The sheet record of the title sprite for `language` (`W_GameState + 0x120`).
    [[nodiscard]] static std::uint32_t titleRecord(Language language);

    /// Shows strings from `strings` (which must outlive the menu; null shows empty texts).
    void setStrings(const GlobalStrings* strings) { m_strings = strings; }
    /// Sends front-end sound cues to `playCue` (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue);
    /// Draws the title through `batch` (not owned; null: no title).
    void setTitleBatch(graphics::SpriteBatch* batch) { m_title.setBatch(batch); }

    /// Opens the menu at `nowMs` in level number `level` with `reason` (empty: string 0xda).
    /// @orig 0x001d23d0 MissionFailedMenu_Open (unknown)
    void open(int level, std::string_view reason, std::uint64_t nowMs);

    /// One frame: input from `frame`'s pad.
    /// @orig 0x001d2b90 MissionFailedMenu_Update (unknown)
    void update(const GuiFrame& frame) override;
    /// Draws the title, the reason, the grid, the usage line and the Yes/No box.
    void render(const GuiCanvas& canvas) const override;

    /// The outcome once a choice has ended the menu; nothing while it is open.
    [[nodiscard]] std::optional<PauseOutcome> outcome() const { return m_outcome; }
    /// The reason's widget.
    [[nodiscard]] const TextWidget& reason() const { return m_reason; }
    /// The grid.
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }
    /// The Yes/No box.
    [[nodiscard]] const YesNoBox& yesNo() const { return m_yesNo; }
    /// The usage line.
    [[nodiscard]] const UsageInfo& usage() const { return m_usage; }
    /// The title sprite.
    [[nodiscard]] const BaseWidget& title() const { return m_title; }

  private:
    // String `id`, or empty without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const;
    // Plays `cue`, if there is a sink.
    void playCue(int cue) const;

    const GlobalStrings* m_strings = nullptr;
    std::function<void(int cue)> m_playCue;
    MenuInput m_input;
    BaseWidget m_title{nullptr, 0};
    TextWidget m_reason;
    OptionGrid m_grid;
    UsageInfo m_usage;
    YesNoBox m_yesNo;
    std::optional<PauseOutcome> m_outcome;
    bool m_open = false;
    bool m_firstFrame = false;
};

} // namespace coney::gui
