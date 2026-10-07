// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

// The blur pulse of a view's screen-effects manager: the dazed, drugged and game-over blur that replaces the view with
// a blurred copy of itself. Research: docs/research/graphics.md#blur-pulse

namespace coney::effects {

/// A view's blur pulse (docs/research/graphics.md#blur-pulse). Started forward it waits out its delay, rises to full
/// over the time given and holds; ended (reverse) it falls from full, or stops at once. Its level sets how many blur
/// passes the view gets (level × look 5's 28, truncated); while it runs past its delay the whole view is replaced by
/// the blurred copy, even at 0 passes (a half-resolution copy).
///
/// **Coney's readings** where the original differs: the delay and the hold are timed by the fixed steps, as the level
/// is, not by a separate real-time clock (they differ only while the game is paused or slowed); one blur pulse, for
/// player 1's view.
class BlurPulse {
  public:
    /// Look 5, the blur pulse's look, as `config_preload2.lua`'s `CfgScrFx` sets it.
    struct Look {
        float inSeconds = 1.25F; ///< `ScreenQueueEffect(4)`'s rise.
        float outSeconds = 4.0F; ///< Its end's fall.
        /// How long a queued pulse holds at full before it ends itself, in ms: the script's 6000 ms as 60 Hz frames
        /// (× 0.06), which the original then compares in milliseconds, so the hold is 360 ms.
        std::uint32_t holdMs = 360;
        int passes = 28;        ///< Blur passes at full level.
        float offsetU = 0.002F; ///< Each pass's offset, U; half of it moves one edge of the copy.
        float offsetV = 0.003F; ///< And V.
    };

    /// Where the pulse is.
    enum class State : std::uint8_t {
        Off,     ///< Nothing drawn.
        Started, ///< Just started or ended: becomes Moving on its next update.
        Moving,  ///< Rising or falling.
        Held,    ///< At full.
    };

    BlurPulse() = default;
    explicit BlurPulse(Look look) : m_look(look) {}

    /// Starts the pulse forward: after `delayMs` (counted from now) it rises to full over `seconds` (at once when 0 or
    /// less) and holds; one already part-way keeps its level and rises over the rest of the time. The death camera
    /// (`CamUseDeathCamera(human, ms, ms2)`: `start(ms / 1000, ms2)`) and the game-over shot (`start(6.5, 1500)`).
    /// @orig 0x0018d058 ScreenFx_StartBlurPulse (ScreenEffectsManager.cpp)
    void start(float seconds, std::uint32_t delayMs);
    /// Ends the pulse (the same function, reversed): from full down to nothing over `seconds`, or off at once when 0
    /// or less (a retry, the death camera once its fade has covered the screen).
    /// @orig 0x0018d058 ScreenFx_StartBlurPulse (ScreenEffectsManager.cpp)
    void end(float seconds);
    /// `ScreenQueueEffect(4)`: starts forward over look 5's in time; at full it holds look 5's hold and then ends
    /// itself over look 5's out time.
    /// Type 4 is handled at 0x0018d57c.
    /// @orig 0x0018d450 ScreenFx_QueueEffect (ScreenEffectsManager.cpp)
    void queueStart();
    /// `ScreenQueueEffect(5, seconds)`: ends over look 5's out time, or at once when `seconds` is 0 or less.
    /// Type 5 is handled at 0x0018d59c.
    /// @orig 0x0018d450 ScreenFx_QueueEffect (ScreenEffectsManager.cpp)
    void queueEnd(float seconds);

    /// One update of `seconds`: the delay, the rise or fall, the hold.
    /// @orig 0x0018d1d0 ScreenFx_DrawBlurPulse (ScreenEffectsManager.cpp)
    void step(float seconds);

    /// The blur passes the view gets after the last update, or nothing when the pulse draws nothing (off, waiting out
    /// its delay, or just started below full).
    [[nodiscard]] std::optional<int> passes() const { return m_passes; }
    /// Whether the pulse runs at all (its state is not Off): the screen effects are then drawn before the HUD
    /// (docs/research/rendering.md#tint).
    [[nodiscard]] bool running() const { return m_state != State::Off; }
    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] float level() const { return m_level; }
    [[nodiscard]] const Look& look() const { return m_look; }
    /// Back to off (a level is unloaded).
    void reset() { *this = BlurPulse{m_look}; }

  private:
    // The shared start: forward or reverse over `seconds`, with `delayMs` and the auto-end flag.
    void begin(float seconds, bool reverse, std::uint32_t delayMs, bool autoEnd);

    Look m_look;
    State m_state = State::Off;
    float m_level = 0.0F;
    float m_rate = 0.0F; // level per second
    std::uint32_t m_delayMs = 0;
    std::uint64_t m_startMs = 0;
    std::uint64_t m_heldMs = 0; // when the level reached full
    bool m_autoEnd = false;     // a queued pulse ends itself after the hold
    std::uint64_t m_nowMs = 0;  // the pulse's clock, advanced by step()
    double m_carryMs = 0.0;     // the part of a millisecond step() has not counted yet
    std::optional<int> m_passes;
};

} // namespace coney::effects
