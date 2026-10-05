// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sdl_input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/pad.h"

namespace coney::platform {

namespace {

// An SDL stick axis at full right or down.
constexpr float kStickAxisFull = 32767.0F;

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
constexpr std::array<Binding<SDL_Scancode>, 20> kKeyboardButtons{{
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
    // F4 holds both sticks in: the debug menu's chord (src/debug/input_gate.h).
    {SDL_SCANCODE_F4, pad::kL3},
    {SDL_SCANCODE_F4, pad::kR3},
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
    // PadSample::sticks is right x, right y, left x, left y; SDL's y is down positive, like the PS2's byte. Each
    // stick is squared off like a DualShock 2's, so a full diagonal is a full push.
    const auto rightStick = stickBytesFromAxes(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX),
                                               SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
    const auto leftStick = stickBytesFromAxes(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX),
                                              SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
    sample.sticks = {rightStick[0], rightStick[1], leftStick[0], leftStick[1]};
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

// The SDL event watch: records each gamepad button and key press as it arrives, for the next sample, so a tap that is
// over before the sample is still seen (SdlInput::notePress). Key repeats are not presses. Always keeps the event.
bool SDLCALL watchPresses(void* userdata, SDL_Event* event) {
    auto* input = static_cast<SdlInput*>(userdata);
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        for (const auto& binding : kGamepadButtons) {
            if (static_cast<int>(binding.source) == static_cast<int>(event->gbutton.button)) {
                input->notePress(event->gbutton.which, binding.bit);
            }
        }
    } else if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        for (const auto& binding : kKeyboardButtons) {
            if (binding.source == event->key.scancode) {
                input->notePress(0, binding.bit);
            }
        }
    }
    return true;
}

} // namespace

std::uint8_t stickByteFromAxis(std::int16_t axis) {
    // Shift the signed range to 0..65535 and keep the high byte: -32768 -> 0, 0 -> 128, 32767 -> 255.
    return static_cast<std::uint8_t>((static_cast<int>(axis) + 32768) >> 8);
}

std::array<std::uint8_t, 2> stickBytesFromAxes(std::int16_t x, std::int16_t y) {
    const float fx = std::max(-1.0F, static_cast<float>(x) / kStickAxisFull);
    const float fy = std::max(-1.0F, static_cast<float>(y) / kStickAxisFull);
    // Scale by length / largest axis: a point on the circle of radius r lands on the square of half-side r.
    const float largest = std::max(std::abs(fx), std::abs(fy));
    const float scale = largest > 0.0F ? std::min(std::hypot(fx, fy), 1.0F) / largest : 0.0F;
    const auto axis = [scale](float value) {
        return static_cast<std::int16_t>(std::clamp(value * scale * kStickAxisFull, -32768.0F, kStickAxisFull));
    };
    return {stickByteFromAxis(axis(fx)), stickByteFromAxis(axis(fy))};
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
    std::unique_ptr<SdlInput> input(new SdlInput());
    input->m_watching = SDL_AddEventWatch(watchPresses, input.get());
    return input;
}

SdlInput::~SdlInput() {
    if (m_watching) {
        SDL_RemoveEventWatch(watchPresses, this);
    }
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

    // Buttons pressed since the last sample count as held in this one, even if they were let go already.
    std::vector<Press> presses;
    {
        const std::scoped_lock lock(m_pressMutex);
        presses.swap(m_presses);
    }
    for (const Press& press : presses) {
        // The keyboard is port 1's; a gamepad that has gone since (not found) has no port.
        std::size_t port = 0;
        if (press.gamepad != 0) {
            const auto found = std::ranges::find(m_gamepads, press.gamepad, &OpenGamepad::id);
            port = found == m_gamepads.end() ? kPadPorts : static_cast<std::size_t>(found - m_gamepads.begin());
        }

        if (port < kPadPorts) {
            samples.at(port).buttons |= press.bit;
        }
    }
    for (PadSample& sample : samples) {
        fillDigitalPressure(sample);
    }
    return samples;
}

void SdlInput::notePress(std::uint32_t gamepad, std::uint16_t bit) {
    const std::scoped_lock lock(m_pressMutex);
    m_presses.push_back(Press{gamepad, bit});
}

} // namespace coney::platform
