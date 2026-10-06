// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "animation/anim_math.h"
#include "world_objects/object_services.h"

// Lock picking: the dial of three turning pins a player times cross presses against, and the outcome at the door.
// Research: docs/research/crimes.md#lockpick, docs/research/objects.md#lock-pick

namespace coney::world_objects {

class Doors;

/// Pins on the dial; good presses in a row that open the lock.
inline constexpr int kLockPins = 3;
/// What a pin turns per frame, radians, times its speed.
inline constexpr float kPinStep = 0.1F;
/// The pins' speeds, first to last.
inline constexpr std::array<int, kLockPins> kPinSpeeds{1, 2, 3};
/// Bonus statistic of a perfect pick: event 1-3.
inline constexpr int kPerfectPickCategory = 1;
inline constexpr int kPerfectPickEvent = 3;

/// The angles a press is judged against at one difficulty: **good** when the pin's angle θ is at least `goodHigh` or at
/// most `goodLow`, **perfect** when at least `perfectHigh` or at most `perfectLow` (negative for no lower band).
struct PinBands {
    float goodHigh;
    float goodLow;
    float perfectHigh;
    float perfectLow;
};

/// The bands by difficulty 0-2 (`LockPickDial_SetDifficulty`).
inline constexpr std::array<PinBands, 3> kPinBands{
    PinBands{5.45F, 0.80F, 5.90F, 0.20F}, PinBands{5.70F, 0.57F, 5.95F, 0.12F}, PinBands{5.90F, 0.35F, 6.05F, -1.0F}};
/// The pins' directions by difficulty (+1 or −1).
inline constexpr std::array<std::array<int, kLockPins>, 3> kPinDirections{
    std::array<int, kLockPins>{1, 1, 1}, std::array<int, kLockPins>{1, -1, 1}, std::array<int, kLockPins>{-1, 1, -1}};

/// What a cross press scored.
enum class PinPress : std::uint8_t { Miss, Good, Perfect };

/// Where a lock pick stands.
enum class LockPickState : std::uint8_t {
    Running,   ///< Picking.
    Succeeded, ///< Three good presses in a row.
    Abandoned, ///< Triangle or another command outside the game's own.
};

/// The HUD dial and the two counters of the mini-game record (`+0x42` good presses in a row, −1 to abandon; `+0x44`
/// perfect ones). Steps once per drawn frame (inferred at 30 a second).
///
/// Research: docs/research/crimes.md#lockpick
class LockPickDial {
  public:
    /// A dial at `difficulty` (the Warrior class's byte `+0x0a` less 1, clamped to 0-2): every pin at π, the first
    /// current, the counters 0.
    /// @orig 0x001b7a18 LockPickDial_SetDifficulty (unknown)
    explicit LockPickDial(int difficulty);

    /// One frame: the current pin turns by 0.1 rad × its speed in its direction, wrapped into [0, 2π).
    /// @orig 0x001b8530 LockPickDial_Draw (unknown)
    void step();
    /// A cross press: judges the current pin. Good (or perfect) counts and moves to the next pin; a miss resets the
    /// counters and puts every pin back at π. Nothing once the pick is over.
    /// @orig 0x002878b8 LockPick_JudgePress (unknown)
    /// @orig 0x001b8d38 LockPickDial_Judge (unknown)
    PinPress press();
    /// Abandons the pick (`+0x42` = −1).
    void abandon();

    /// Where the pick stands (`MiniGame_Update`'s test).
    [[nodiscard]] LockPickState state() const;
    /// Whether every press so far was perfect: three make a perfect pick.
    [[nodiscard]] bool perfect() const { return m_perfects >= kLockPins; }
    /// The pins' angles, radians.
    [[nodiscard]] const std::array<float, kLockPins>& pins() const { return m_pins; }
    /// The pin the next press judges.
    [[nodiscard]] int currentPin() const { return m_good < 0 ? 0 : m_good % kLockPins; }
    /// Good presses in a row (−1 once abandoned); perfect ones.
    [[nodiscard]] int good() const { return m_good; }
    [[nodiscard]] int perfects() const { return m_perfects; }
    /// The difficulty, 0-2.
    [[nodiscard]] int difficulty() const { return m_difficulty; }

  private:
    // Every pin back at π.
    void resetPins();

    int m_difficulty;
    std::array<float, kLockPins> m_pins{};
    int m_good = 0;
    int m_perfects = 0;
};

/// The script functions `CfgSetLockPickHandler` and `CfgSetLockPickStageFailHandler` name; empty for none.
struct LockPickHandlers {
    std::string start{};
    std::string stop{};
    std::string success{};
    std::string stageFail{};
};

/// One player's lock pick at a door, from triangle to the outcome: the start callback, the dial, the stage-fail
/// callback on a miss, and at the end the door unlocked and swung open (success) or the abandon counted.
///
/// Research: docs/research/crimes.md#lockpick
class LockPick {
  public:
    /// Starts `human` (standing at `humanAt`) picking `door` at `difficulty`: the start callback with both handles.
    /// @orig 0x0022d790 LockPick_Start (unknown)
    LockPick(const LockPickHandlers& handlers, double human, anim::Vec3 humanAt, double door, int difficulty,
             ObjectWorld& world);

    /// One frame of the dial.
    void step() { m_dial.step(); }
    /// A cross press; a miss plays the click and runs the stage-fail callback. Ends the pick on the third good press.
    PinPress press(Doors& doors, ObjectWorld& world);
    /// Triangle or another command outside the game's: abandons and ends the pick.
    void abandon(Doors& doors, ObjectWorld& world);

    /// Where it stands.
    [[nodiscard]] LockPickState state() const { return m_dial.state(); }
    /// The dial.
    [[nodiscard]] const LockPickDial& dial() const { return m_dial; }
    /// The human and the door.
    [[nodiscard]] double human() const { return m_human; }
    [[nodiscard]] double door() const { return m_door; }

  private:
    // The outcome, once: success unlocks and opens the door (a break-in unless perfect), abandoning counts at the door
    // (the third reports a break-in).
    // @orig 0x0022d908 LockPick_End (unknown)
    void end(Doors& doors, ObjectWorld& world);

    LockPickHandlers m_handlers;
    double m_human;
    anim::Vec3 m_humanAt;
    double m_door;
    LockPickDial m_dial;
    bool m_ended = false;
};

} // namespace coney::world_objects
