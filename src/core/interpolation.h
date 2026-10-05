// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <numbers>

// Blending between the last two simulation steps for rendering (docs/guides/conventions.md#update-and-render). Every
// helper returns its `to` value exactly at t = 1 and its `from` value exactly at t = 0, so a render at alpha 1 (test
// mode, `--fps-cap 30`) draws exactly the newest state, bit for bit, as if there were no interpolation at all.

namespace coney {

/// Linear blend from `from` (t = 0) to `to` (t = 1). Written as (1 - t)·from + t·to so both ends are exact.
[[nodiscard]] inline float lerp(float from, float to, float t) { return ((1.0F - t) * from) + (t * to); }

/// Blend of two angles in radians the short way round: from 3.1 to -3.1 goes through π, not through 0. Returns `to`
/// itself at t >= 1 and `from` at t <= 0, so the ends are exact even across the wrap.
[[nodiscard]] inline float lerpAngle(float from, float to, float t) {
    if (t >= 1.0F) {
        return to;
    }
    if (t <= 0.0F) {
        return from;
    }
    const float delta = std::remainder(to - from, 2.0F * std::numbers::pi_v<float>);
    return from + (delta * t);
}

/// Blend of a time that loops with `period` (an animation clip's playhead), going forwards from `from` to `to`: when
/// `to` is less than `from` the clip wrapped round in between, so the blend runs on past the end and wraps too.
/// Returns `to` itself at t >= 1 and `from` at t <= 0. A period of 0 or less returns `to`.
[[nodiscard]] inline float lerpLooping(float from, float to, float t, float period) {
    if (t >= 1.0F || period <= 0.0F) {
        return to;
    }
    if (t <= 0.0F) {
        return from;
    }
    const float end = to >= from ? to : to + period;
    const float blended = from + ((end - from) * t);
    return blended >= period ? blended - period : blended;
}

/// A value kept at the last two simulation steps, for rendering between them: what a mode keeps for anything that
/// moves continuously (a camera, a character's position, a playhead).
///
/// The rule: at the start of each update the mode calls commit(), which makes the newest value the previous one,
/// then changes current(). render() reads previous() and current() and blends them with the frame's alpha; it never
/// writes. reset() makes both the same, for a jump that must not be blended (a teleport, a new clip).
template <typename T> class Interpolated {
  public:
    /// Both steps hold `value`.
    explicit Interpolated(T value) : m_previous(value), m_current(value) {}

    /// Starts a step: the newest value becomes the previous one.
    void commit() { m_previous = m_current; }
    /// Jumps to `value` with nothing to blend from.
    void reset(T value) {
        m_previous = value;
        m_current = value;
    }

    /// The value as of the newest step, for the simulation to read and change.
    [[nodiscard]] T& current() { return m_current; }
    /// The value as of the newest step.
    [[nodiscard]] const T& current() const { return m_current; }
    /// The value as of the step before.
    [[nodiscard]] const T& previous() const { return m_previous; }

  private:
    T m_previous;
    T m_current;
};

} // namespace coney
