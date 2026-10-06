// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gamemodes/front_end_services.h"
#include "gamemodes/pause_mode.h"
#include "gamemodes/play_overlay.h"
#include "gui/global_strings.h"
#include "gui/rumble_mode_gui/rumble_intro.h"
#include "warriors/game_state.h"

namespace coney {

/// The Rumble intro (gui::RumbleIntro) as a screen over play: `ShowRumbleModeIntro` opens it (show()), gameplay steps
/// and draws it (PlayOverlay), and when the countdown ends it calls the script's `onDone` through the front end's
/// services. Its fonts are loaded when it opens and released when it closes, as MenuLayer loads a mode's.
///
/// Research: docs/research/rumble.md#intro
class RumbleIntroLayer final : public PlayOverlay {
  public:
    /// Fonts from `loadSheet`, strings from `strings`, the script call through `services`, the gang lines' packs and
    /// the random lines from `state`; each must outlive the layer. `log` gets a line when it opens and ends.
    RumbleIntroLayer(PauseSheetLoader loadSheet, const gui::GlobalStrings& strings, FrontEndServices& services,
                     GameState& state, std::function<void(std::string_view)> log);

    /// Plays the announcer and the stings through `sounds` from now on.
    void setSounds(gui::RumbleIntroSounds sounds) { m_intro.setSounds(std::move(sounds)); }

    /// `ShowRumbleModeIntro(onDone, names)`: the intro opens on the next frame of play. **Coney choice**: Coney runs
    /// the start callback, which asks for it, before the level loads, so the intro waits for play to begin.
    /// @orig 0x001b5f88 ShowRumbleModeIntro (unknown)
    void show(std::string_view onDone, std::span<const std::string> names);

    void playFrame(const FrameTime& frame, const Pads& pads) override;
    [[nodiscard]] bool showing() const override { return m_intro.isOpen(); }
    /// Whether an intro was asked for and waits for play.
    [[nodiscard]] bool pending() const { return m_pending.has_value(); }
    void draw(graphics::RenderDevice& device) override;
    /// Closes the intro and forgets one asked for; the fonts go.
    void levelEnded() override;

    /// The intro.
    [[nodiscard]] const gui::RumbleIntro& intro() const { return m_intro; }

  private:
    PauseSheetLoader m_loadSheet;
    FrontEndServices& m_services;
    GameState& m_state;
    std::function<void(std::string_view)> m_log;
    gui::RumbleIntro m_intro;
    MenuLayer m_layer;
    bool m_loaded = false;
    // What show() asked for, until the next frame of play opens it.
    struct Pending {
        std::string onDone;
        std::vector<std::string> names;
    };
    std::optional<Pending> m_pending;
};

} // namespace coney
