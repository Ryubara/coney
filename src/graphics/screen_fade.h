// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "graphics/render_device.h"

namespace coney::graphics {

/// The screen fade of the first screen-effects manager (`ScreenEffectsManager`, 0x220 bytes, the first of two at
/// `0x005fdeb8`), as far as the front end uses it: `ScreenQueueEffect(type, seconds)` starts a fade to or from black,
/// and two fields tell the menus about it: whether a fade is running (`+0x1d4`) and the fade level (`+0x1d8`).
/// PM_Greet restarts its idle time and keeps its prompt lit while the screen is fading or faded; PM_Mode ignores input
/// while the level is not 0. Pad input never touches them.
///
/// Coney's choices (docs/research/frontend.md#profile-manager gives the fields' roles, not their arithmetic): the level
/// is 0 for a clear screen and 1 for black; a fade in runs from 1 to 0 and a fade out from 0 to 1, linearly over the
/// given time of game time; a new fade replaces one that is running; any type but 0 and 1 is ignored. The fade is drawn
/// as a black quad over the whole logical screen with the level as its opacity.
///
/// Research: docs/research/frontend.md#profile-manager, docs/research/scripting.md#level100lua-the-front-end
class ScreenFade {
  public:
    /// `ScreenQueueEffect`'s type that fades in (from black).
    static constexpr int kFadeIn = 0;
    /// `ScreenQueueEffect`'s type that fades out (to black).
    static constexpr int kFadeOut = 1;

    /// Starts a fade of `type` lasting `seconds`, at game time `nowMs`. A fade of 0 s or less jumps to its end.
    /// @orig 0x0018cc60 ScreenQueueEffect (unknown)
    void queue(int type, double seconds, std::uint64_t nowMs);

    /// Advances the fade to game time `nowMs`; it stops running at its end.
    void update(std::uint64_t nowMs);

    /// Whether a fade is running (`+0x1d4`).
    [[nodiscard]] bool running() const { return m_running; }
    /// The fade level (`+0x1d8`): 0 clear, 1 black.
    [[nodiscard]] float level() const { return m_level; }
    /// Whether the screen is fading or not clear: what PM_Greet treats as "a fade in progress".
    [[nodiscard]] bool active() const { return m_running || m_level > 0.0F; }

    /// Draws the fade over the logical screen through `device`: nothing while the screen is clear.
    void render(RenderDevice& device) const;

  private:
    bool m_running = false;
    float m_level = 0.0F;
    float m_from = 0.0F;
    float m_to = 0.0F;
    std::uint64_t m_startMs = 0;
    std::uint64_t m_durationMs = 0;
};

} // namespace coney::graphics
