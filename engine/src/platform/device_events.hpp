#pragma once

// Keyboard and gamepad events as the platform layer reports them. No SDL types: the input layer (src/input/) reads
// these without seeing SDL.

#include <cstdint>
#include <string>

namespace coney::platform {

// A gamepad's id while it stays connected; a gamepad that is pulled and put back gets a new one.
using GamepadId = std::uint32_t;

// Buttons by position, in SDL's gamepad order: south is the bottom face button.
enum class GamepadButton : std::uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Misc1,
    RightPaddle1,
    LeftPaddle1,
    RightPaddle2,
    LeftPaddle2,
    Touchpad,
    Count,
};

// Sticks go from -32768 to 32767, right and down positive; triggers from 0 to 32767.
enum class GamepadAxis : std::uint8_t {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

// A key went down or up; key repeats are not reported. `key` is the key's name by its position on the keyboard
// ("W", "Space", "Left Shift").
struct KeyChanged {
    std::string key;
    bool down = false;
};
struct GamepadAdded {
    GamepadId id = 0;
    std::string name;
};
struct GamepadRemoved {
    GamepadId id = 0;
};
struct GamepadButtonChanged {
    GamepadId id = 0;
    GamepadButton button = GamepadButton::South;
    bool down = false;
};
struct GamepadAxisMoved {
    GamepadId id = 0;
    GamepadAxis axis = GamepadAxis::LeftX;
    std::int16_t value = 0;
};

} // namespace coney::platform
