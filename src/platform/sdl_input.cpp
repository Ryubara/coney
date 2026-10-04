// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sdl_input.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <SDL3/SDL.h>

#include "core/pad.h"

namespace coney::platform {

namespace {

// A pad button and the SDL thing that drives it.
template <typename Source> struct Binding {
    Source source;
    std::uint16_t bit;
};

// Gamepad buttons, by SDL's positional names. L2 and R2 are triggers (axes), handled apart.
constexpr std::array<Binding<SDL_GamepadButton>, 14> kGamepadButtons{{
    {SDL_GAMEPAD_BUTTON_SOUTH, pad::kCross},
    {SDL_GAMEPAD_BUTTON_EAST, pad::kCircle},
    {SDL_GAMEPAD_BUTTON_WEST, pad::kSquare},
    {SDL_GAMEPAD_BUTTON_NORTH, pad::kTriangle},
    {SDL_GAMEPAD_BUTTON_BACK, pad::kSelect},
    {SDL_GAMEPAD_BUTTON_START, pad::kStart},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, pad::kL3},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, pad::kR3},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, pad::kL1},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, pad::kR1},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, pad::kUp},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, pad::kRight},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, pad::kDown},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, pad::kLeft},
}};

// The keyboard on port 1, a Coney choice (docs/guides/building.md#controls). Scancodes are key positions, so the
// layout is the same on a QWERTY, AZERTY or any other keyboard: arrows for the d-pad, the I J K L diamond for the
// face buttons as they sit on the pad, Q and E for the shoulders above them.
constexpr std::array<Binding<SDL_Scancode>, 18> kKeyboardButtons{{
    {SDL_SCANCODE_UP, pad::kUp},
    {SDL_SCANCODE_RIGHT, pad::kRight},
    {SDL_SCANCODE_DOWN, pad::kDown},
    {SDL_SCANCODE_LEFT, pad::kLeft},
    {SDL_SCANCODE_K, pad::kCross},
    {SDL_SCANCODE_L, pad::kCircle},
    {SDL_SCANCODE_J, pad::kSquare},
    {SDL_SCANCODE_I, pad::kTriangle},
    {SDL_SCANCODE_RETURN, pad::kStart},
    {SDL_SCANCODE_KP_ENTER, pad::kStart},
    {SDL_SCANCODE_BACKSPACE, pad::kSelect},
    {SDL_SCANCODE_Q, pad::kL1},
    {SDL_SCANCODE_E, pad::kR1},
    {SDL_SCANCODE_1, pad::kL2},
    {SDL_SCANCODE_3, pad::kR2},
    {SDL_SCANCODE_F, pad::kL3},
    {SDL_SCANCODE_H, pad::kR3},
    {SDL_SCANCODE_SPACE, pad::kCross},
}};

// Full pressure on every pressure-sensitive button in `buttons`, as a digital button gives; existing pressures that
// are higher (a trigger's) are kept.
void fillDigitalPressure(PadSample& sample) {
    for (std::size_t i = 0; i < pad::kPressureCount; ++i) {
        if ((sample.buttons & pad::kPressureButtons.at(i)) != 0) {
            sample.pressure.at(i) = 255;
        }
    }
}

// Reads one gamepad into a sample: buttons, triggers as L2 and R2 with their pressure, and both sticks.
PadSample readGamepad(SDL_Gamepad* gamepad) {
    PadSample sample;
    sample.connected = true;
    for (const auto& binding : kGamepadButtons) {
        if (SDL_GetGamepadButton(gamepad, binding.source)) {
            sample.buttons |= binding.bit;
        }
    }
    const std::uint8_t left = pressureFromTrigger(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    const std::uint8_t right = pressureFromTrigger(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    sample.pressure.at(static_cast<std::size_t>(pad::Pressure::L2)) = left;
    sample.pressure.at(static_cast<std::size_t>(pad::Pressure::R2)) = right;
    if (left >= kTriggerHeldPressure) {
        sample.buttons |= pad::kL2;
    }
    if (right >= kTriggerHeldPressure) {
        sample.buttons |= pad::kR2;
    }
    fillDigitalPressure(sample);
    // PadSample::sticks is right x, right y, left x, left y; SDL's y is down positive, like the PS2's byte.
    sample.sticks = {stickByteFromAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX)),
                     stickByteFromAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY)),
                     stickByteFromAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)),
                     stickByteFromAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY))};
    return sample;
}

// One stick axis from two keys: full deflection towards whichever is held, centred for neither or both.
std::uint8_t keyAxis(const bool* keys, SDL_Scancode negative, SDL_Scancode positive) {
    const int value = (keys[positive] ? 1 : 0) - (keys[negative] ? 1 : 0);
    return value < 0 ? std::uint8_t{0} : value > 0 ? std::uint8_t{255} : pad::kStickCentre;
}

// Adds the keyboard to port 1's sample: its buttons on top of the gamepad's, and W A S D as the left stick, which
// replace the gamepad's left stick while any of them is held.
void addKeyboard(PadSample& sample) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr) {
        return;
    }
    for (const auto& binding : kKeyboardButtons) {
        if (keys[binding.source]) {
            sample.buttons |= binding.bit;
        }
    }
    fillDigitalPressure(sample);
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_D]) {
        sample.sticks[2] = keyAxis(keys, SDL_SCANCODE_A, SDL_SCANCODE_D);
        sample.sticks[3] = keyAxis(keys, SDL_SCANCODE_W, SDL_SCANCODE_S);
    }
}

} // namespace

std::uint8_t stickByteFromAxis(std::int16_t axis) {
    // Shift the signed range to 0..65535 and keep the high byte: -32768 -> 0, 0 -> 128, 32767 -> 255.
    return static_cast<std::uint8_t>((static_cast<int>(axis) + 32768) >> 8);
}

std::uint8_t pressureFromTrigger(std::int16_t axis) {
    const int value = std::max(0, static_cast<int>(axis));
    return static_cast<std::uint8_t>(value * 255 / 32767);
}

std::expected<std::unique_ptr<SdlInput>, Error> SdlInput::start() {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        return fail(ErrorCode::PlatformFailure,
                    std::string("could not start gamepad input: SDL_InitSubSystem(SDL_INIT_GAMEPAD) failed: ") +
                        SDL_GetError());
    }
    // The constructor is private, so make_unique cannot reach it.
    return std::unique_ptr<SdlInput>(new SdlInput());
}

SdlInput::~SdlInput() {
    for (const OpenGamepad& gamepad : m_gamepads) {
        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(gamepad.handle));
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void SdlInput::refreshGamepads() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    const std::span<const SDL_JoystickID> present(ids, ids != nullptr ? static_cast<std::size_t>(count) : 0);

    // Close the gamepads that went away; the others keep their order, and so their ports.
    std::erase_if(m_gamepads, [present](const OpenGamepad& gamepad) {
        if (std::ranges::find(present, gamepad.id) != present.end()) {
            return false;
        }
        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(gamepad.handle));
        return true;
    });
    // Open the new ones, after the others.
    for (const SDL_JoystickID id : present) {
        if (std::ranges::find(m_gamepads, id, &OpenGamepad::id) != m_gamepads.end()) {
            continue;
        }
        if (SDL_Gamepad* opened = SDL_OpenGamepad(id); opened != nullptr) {
            m_gamepads.push_back(OpenGamepad{id, opened});
        }
    }
    SDL_free(ids);
}

PortSamples SdlInput::sample(std::uint64_t /*frame*/) {
    refreshGamepads();
    PortSamples samples{};
    for (std::size_t port = 0; port < kPadPorts && port < m_gamepads.size(); ++port) {
        samples.at(port) = readGamepad(static_cast<SDL_Gamepad*>(m_gamepads[port].handle));
    }
    // Port 1 always has the keyboard.
    samples[0].connected = true;
    addKeyboard(samples[0]);
    return samples;
}

} // namespace coney::platform
