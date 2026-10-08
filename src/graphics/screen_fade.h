// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "graphics/render_device.h"

namespace coney::graphics {

/// The screen fade of the screen-effects manager (`ScreenEffectsManager`, `0x005fdeb8`; the second manager, for a
/// second view, fades the same way and Coney has one view): `ScreenQueueEffect(type, seconds)` starts a fade to or
/// from black, and two fields tell the menus about it: whether a fade is running (`+0x1d4`) and the fade level
/// (`+0x1d8`), 0 clear and 1 black.
///
/// - **Start** (`0x0018cc60`): a fade in runs from 1 (black) down to the base alpha, 0, over `t` seconds; a fade out
///   from 0 up to 1 over `t − 0.2` seconds when `t` > 0.2 (so a "1.0 s" fade out takes 0.8 s), else over `t`. A fade
///   of no length jumps to its end. The fade is set to its start level at once, so a fade in starts at full black.
/// - **Each frame** (`0x0018ce58`): the first frame after a request only marks the fade running (state 1 → 2); after
///   that the level moves at the fade's rate, clamped to [0, 1], and the fade stops running at its end.
/// - **Drawn** as a black quad over the screen with alpha level × 255.
///
/// Coney's choices: the level follows game time since the frame that marked it running (the original adds its rate ×
/// the frame time, an argument not traced; at runtime a 1.0 s fade out went black faster, an open question on the
/// page); a new fade replaces one that is running; any type but 0 and 1 is ignored here (the letterbox and the blur
/// pulse are other effects); the fade out's hiding of the HUD (`0x001b2658`) is not done.
///
/// Research: docs/research/frontend.md#fades, docs/research/graphics.md#screen-effects
class ScreenFade {
  public:
    /// `ScreenQueueEffect`'s type that fades in (from black).
    static constexpr int kFadeIn = 0;
    /// `ScreenQueueEffect`'s type that fades out (to black).
    static constexpr int kFadeOut = 1;
    /// How much shorter a fade out runs than it is asked for, when it is longer than this.
    static constexpr double kFadeOutShortening = 0.2;

    /// Starts a fade of `type` lasting `seconds`, asked for at game time `nowMs`.
    /// @orig 0x0018cc60 ScreenQueueEffect (ScreenEffectsManager.cpp)
    void queue(int type, double seconds, std::uint64_t nowMs);

    /// Advances the fade to game time `nowMs`: once a frame, before the fade is drawn.
    /// @orig 0x0018ce58 ScreenEffects_UpdateFade (ScreenEffectsManager.cpp)
    void update(std::uint64_t nowMs);

    /// Whether a fade is running (`+0x1d4`), including the frame that only marks it.
    [[nodiscard]] bool running() const { return m_state != State::Idle; }
    /// The fade level (`+0x1d8`): 0 clear, 1 black.
    [[nodiscard]] float level() const { return m_level; }
    /// Whether the screen is fading or not clear: what PM_Greet treats as "a fade in progress".
    [[nodiscard]] bool active() const { return running() || m_level > 0.0F; }
    /// How long the running fade takes to reach its end, in milliseconds (0 when none runs).
    [[nodiscard]] std::uint64_t durationMs() const { return m_durationMs; }

    /// Draws the fade over the whole picture through `device`: nothing while the screen is clear.
    void render(RenderDevice& device) const { draw(device, m_level); }

    /// Draws a fade of `level` (0 clear, 1 black) over the whole picture: nothing at 0 or less. A render that blends
    /// the level of the last two steps draws through this.
    static void draw(RenderDevice& device, float level);
    /// Draws `colour` over the whole picture (RenderDevice::drawViewQuads()), blended by its alpha (0-255): the fade's
    /// quad, which the level's screen tint shares (effects::ScreenTint, docs/research/rendering.md#tint).
    static void drawWash(RenderDevice& device, Rgba colour);

  private:
    // Idle; asked for, waiting for the frame that marks it running; running.
    enum class State : std::uint8_t { Idle, Requested, Running };

    State m_state = State::Idle;
    float m_level = 0.0F;
    float m_from = 0.0F;
    float m_to = 0.0F;
    std::uint64_t m_startMs = 0;
    std::uint64_t m_durationMs = 0;
};

} // namespace coney::graphics
