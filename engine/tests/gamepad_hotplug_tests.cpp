// End to end through SDL: a virtual gamepad is attached, pressed and detached, and the platform layer's events drive
// the input layer. Runs under SDL's offscreen video driver.

#include "input/bindings.hpp"
#include "input/input_system.hpp"
#include "platform/platform.hpp"

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace coney;

namespace {

struct Fixture {
    core::SteadyClock clock;
    core::Logger logger{clock, core::LogLevel::Warning};
};

// Feeds every pending platform event to the input layer and returns them.
std::vector<platform::Event> pump(platform::Platform& platform, input::InputSystem& input) {
    std::vector<platform::Event> seen;
    for (int round = 0; round < 3; ++round) { // gamepad events can take an update to arrive
        SDL_PumpEvents();
        while (auto event = platform.poll_event()) {
            input.handle(*event);
            seen.push_back(std::move(*event));
        }
    }
    return seen;
}

SDL_JoystickID attach_virtual_gamepad() {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    desc.name = "Coney test pad";
    return SDL_AttachVirtualJoystick(&desc);
}

template <typename T> bool holds(const std::vector<platform::Event>& events) {
    for (const auto& e : events) {
        if (std::holds_alternative<T>(e)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("the default key names are SDL key names") {
    const input::Bindings bindings = input::default_bindings();
    for (const auto& keys : bindings.keyboard) {
        for (const auto& name : keys) {
            INFO(name);
            const SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
            REQUIRE(scancode != SDL_SCANCODE_UNKNOWN);
            CHECK(name == SDL_GetScancodeName(scancode));
        }
    }
}

TEST_CASE("a virtual gamepad is added, drives player 1, is pulled and given back") {
    Fixture f;
    auto platform = platform::Platform::start(f.logger);
    REQUIRE(platform);
    platform::Platform& p = **platform;
    input::InputSystem input;
    input.set_command_bindings({{18, game::TriggerKind::Pressed, game::button::kCross, 0}});
    (void)pump(p, input);
    (void)input.take_events();

    const SDL_JoystickID id = attach_virtual_gamepad();
    REQUIRE(id != 0);
    auto seen = pump(p, input);
    REQUIRE(holds<platform::GamepadAdded>(seen));
    CHECK(p.open_gamepads() == 1);
    auto events = input.take_events();
    REQUIRE(events.size() == 1);
    CHECK(std::get<input::ControllerAssigned>(events[0]).player == 1);
    CHECK(input.gamepad_of(1) == id);

    SDL_Joystick* joystick = SDL_OpenJoystick(id);
    REQUIRE(joystick != nullptr);
    REQUIRE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
    REQUIRE(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX));
    seen = pump(p, input);
    CHECK(holds<platform::GamepadButtonChanged>(seen));
    input.tick();
    CHECK(input.player(1).buttons == game::button::kCross);
    CHECK(input.player(1).command == 18);
    CHECK(input.player(1).sticks.left.x == 1.0f);
    SDL_CloseJoystick(joystick);

    REQUIRE(SDL_DetachVirtualJoystick(id));
    seen = pump(p, input);
    CHECK(holds<platform::GamepadRemoved>(seen));
    CHECK(p.open_gamepads() == 0);
    events = input.take_events();
    REQUIRE(events.size() == 1);
    CHECK(std::get<input::ControllerLost>(events[0]).player == 1);

    // Put back: a new gamepad, given player 1 when START is pressed and released on it.
    const SDL_JoystickID again = attach_virtual_gamepad();
    REQUIRE(again != 0);
    (void)pump(p, input);
    CHECK(input.take_events().empty());
    joystick = SDL_OpenJoystick(again);
    REQUIRE(joystick != nullptr);
    REQUIRE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, true));
    (void)pump(p, input);
    REQUIRE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, false));
    (void)pump(p, input);
    events = input.take_events();
    REQUIRE(events.size() == 1);
    const auto& restored = std::get<input::ControllerRestored>(events[0]);
    CHECK(restored.player == 1);
    CHECK(restored.gamepad == again);
    SDL_CloseJoystick(joystick);
    REQUIRE(SDL_DetachVirtualJoystick(again));
    (void)pump(p, input);
}
