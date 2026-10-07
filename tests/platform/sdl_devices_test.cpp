// SPDX-License-Identifier: GPL-3.0-or-later
// Device hot-plug: gamepads connected and pulled out while the input is sampled, and the sound device lost while it
// plays (docs/guides/building.md#controls, docs/guides/building.md#sound). Headless and CI-safe: SDL's virtual
// joysticks stand in for gamepads and its dummy audio driver for the sound card; a test whose driver is missing skips.
#include <array>
#include <cstdint>
#include <memory>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "platform/audio_output.h"
#include "platform/sdl_input.h"

namespace pad = coney::pad;
using coney::platform::SdlInput;

namespace {

// Only SDL's virtual joysticks: a real gamepad on the machine running the tests must not take a port. The Linux and
// Android drivers have no switch, but CI machines have no gamepads.
void useVirtualGamepadsOnly() {
    for (const char* hint :
         {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_RAWINPUT, SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_DIRECTINPUT,
          SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_MFI, SDL_HINT_JOYSTICK_IOKIT, SDL_HINT_JOYSTICK_GAMEINPUT}) {
        SDL_SetHint(hint, "0");
    }
}

// A virtual gamepad, attached at construction and detached (if still attached) at destruction. It holds the joystick
// open so the test can press its buttons.
class VirtualPad {
  public:
    VirtualPad() {
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Coney test pad";
        m_id = SDL_AttachVirtualJoystick(&desc);
        m_joystick = m_id != 0 ? SDL_OpenJoystick(m_id) : nullptr;
    }
    ~VirtualPad() { detach(); }
    VirtualPad(const VirtualPad&) = delete;
    VirtualPad& operator=(const VirtualPad&) = delete;
    VirtualPad(VirtualPad&&) = delete;
    VirtualPad& operator=(VirtualPad&&) = delete;

    [[nodiscard]] std::uint32_t id() const { return m_id; }
    [[nodiscard]] bool attached() const { return m_joystick != nullptr; }

    // Sets a button and lets SDL see it, which sends the gamepad's button event.
    void setButton(SDL_GamepadButton button, bool down) {
        SDL_SetJoystickVirtualButton(m_joystick, button, down);
        SDL_UpdateJoysticks();
    }

    // Pulls the pad out: SDL marks it gone at once and queues the removal events.
    void detach() {
        if (m_joystick != nullptr) {
            SDL_CloseJoystick(m_joystick);
            m_joystick = nullptr;
        }
        if (m_id != 0) {
            SDL_DetachVirtualJoystick(m_id);
            m_id = 0;
        }
    }

  private:
    SDL_JoystickID m_id = 0;
    SDL_Joystick* m_joystick = nullptr;
};

// What the window's event pump does each frame: run SDL's device updates and drain the queue.
void pumpEvents() {
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}

// Starts SDL's input with only virtual gamepads, or nothing when SDL refuses (the test then skips).
std::unique_ptr<SdlInput> startInput() {
    useVirtualGamepadsOnly();
    auto started = SdlInput::start();
    return started ? std::move(*started) : nullptr;
}

} // namespace

TEST_CASE("gamepads take the first free port and keep it while the other one comes and goes", "[sdl_devices]") {
    auto input = startInput();
    if (!input) {
        SKIP("SDL's gamepad support did not start");
    }
    (void)input->sample(0);
    CHECK(input->portGamepads() == std::array<std::uint32_t, 2>{0, 0});

    auto first = std::make_unique<VirtualPad>();
    REQUIRE(first->attached());
    pumpEvents();
    auto samples = input->sample(1);
    CHECK(input->portGamepads() == std::array<std::uint32_t, 2>{first->id(), 0});
    CHECK(samples[0].connected);
    CHECK_FALSE(samples[1].connected);

    VirtualPad second;
    pumpEvents();
    samples = input->sample(2);
    CHECK(input->portGamepads() == std::array<std::uint32_t, 2>{first->id(), second.id()});
    CHECK(samples[1].connected);

    // Port 1's pad pulled out: port 1 keeps the keyboard, the second pad stays on port 2.
    first.reset();
    pumpEvents();
    samples = input->sample(3);
    CHECK(input->portGamepads() == std::array<std::uint32_t, 2>{0, second.id()});
    CHECK(samples[0].connected);
    CHECK(samples[1].connected);

    // The next pad connected fills port 1 again.
    VirtualPad third;
    pumpEvents();
    (void)input->sample(4);
    CHECK(input->portGamepads() == std::array<std::uint32_t, 2>{third.id(), second.id()});
}

TEST_CASE("a third gamepad waits for a free port", "[sdl_devices]") {
    auto input = startInput();
    if (!input) {
        SKIP("SDL's gamepad support did not start");
    }
    VirtualPad first;
    auto second = std::make_unique<VirtualPad>();
    VirtualPad third;
    pumpEvents();
    (void)input->sample(0);
    CHECK(input->portGamepads() == std::array<std::uint32_t, 2>{first.id(), second->id()});
    second.reset();
    pumpEvents();
    (void)input->sample(1);
    CHECK(input->portGamepads() == std::array<std::uint32_t, 2>{first.id(), third.id()});
}

TEST_CASE("a gamepad pulled out between two samples reads as nothing, not stale", "[sdl_devices]") {
    auto input = startInput();
    if (!input) {
        SKIP("SDL's gamepad support did not start");
    }
    VirtualPad pad1;
    pumpEvents();
    (void)input->sample(0);
    REQUIRE(input->portGamepads()[0] == pad1.id());

    // A press, then the pad goes before the next sample and before any event pump: the press belongs to no port.
    pad1.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
    pad1.detach();
    const auto samples = input->sample(1);
    CHECK(input->portGamepads()[0] == 0);
    CHECK(samples[0].connected); // the keyboard's port
    CHECK((samples[0].buttons & pad::kCross) == 0);
    pumpEvents();
    (void)input->sample(2);
    CHECK(input->portGamepads()[0] == 0);
}

TEST_CASE("gamepads attached and detached over and over while the input is sampled", "[sdl_devices]") {
    auto input = startInput();
    if (!input) {
        SKIP("SDL's gamepad support did not start");
    }
    std::array<std::unique_ptr<VirtualPad>, 3> pads;
    // A fixed pattern (no randomness): each step attaches or detaches one pad, pressing a button now and then, with
    // the event pump sometimes skipped so a sample runs between a removal and its events.
    for (std::uint64_t step = 0; step < 300; ++step) {
        auto& slot = pads.at(step % pads.size());
        if (slot) {
            if (step % 4 == 1) {
                slot->setButton(SDL_GAMEPAD_BUTTON_EAST, true);
            }
            slot.reset();
        } else {
            slot = std::make_unique<VirtualPad>();
            REQUIRE(slot->attached());
            slot->setButton(SDL_GAMEPAD_BUTTON_SOUTH, step % 3 == 0);
        }
        if (step % 5 != 0) {
            pumpEvents();
        }
        const auto samples = input->sample(step);
        CHECK(samples[0].connected);
        // Every port's gamepad is one of the pads still attached.
        for (const std::uint32_t id : input->portGamepads()) {
            if (id == 0) {
                continue;
            }
            bool found = false;
            for (const auto& other : pads) {
                found = found || (other && other->id() == id);
            }
            CHECK(found);
        }
    }
    pumpEvents();
    (void)input->sample(300);
    // Pads still attached when the input stops are closed by it; SDL detaches them afterwards.
    input.reset();
}

TEST_CASE("the sound keeps playing after its device is lost", "[sdl_devices]") {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    auto output = coney::platform::AudioOutput::start(coney::platform::AudioSink::Device);
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
    if (!output) {
        SKIP("SDL's dummy audio driver did not start: " + output.error().message);
    }
    auto& audio = **output;
    audio.setTestTone(true);
    const std::uint32_t device = audio.deviceId();
    REQUIRE(device != 0);

    // The device goes away as a removed USB headset does when SDL cannot move the sound to another device.
    SDL_Event removed{};
    removed.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
    removed.adevice.which = device;
    REQUIRE(SDL_PushEvent(&removed));
    pumpEvents();
    for (std::uint32_t frame = 0; frame < 3; ++frame) {
        audio.endFrame(1);
    }
    CHECK(audio.deviceId() != 0);
    CHECK(audio.deviceId() != device);
    CHECK(audio.deviceReopens() == 1);
    CHECK(audio.testTone());

    // Other devices' events leave it alone.
    removed.adevice.which = audio.deviceId() + 1000;
    REQUIRE(SDL_PushEvent(&removed));
    pumpEvents();
    audio.endFrame(1);
    CHECK(audio.deviceReopens() == 1);
}
