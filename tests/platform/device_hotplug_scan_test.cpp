// SPDX-License-Identifier: GPL-3.0-or-later
// A hidden window's renderer, and the code that starts SDL's video only to count displays, turn off SDL's keyboard and
// mouse hotplug scan: its thread asks every HID device for its name, and the video's quit waits for it, which made a
// run that starts the renderer several times (coney.frame_copies_upright) wait for the machine's slowest device
// (#1365). SDL reads hints without a display, so this runs on CI.
#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include "platform/render_engine.h"

namespace {

// The hint SDL's Windows video reads as it starts (it has no named constant for it).
constexpr const char* kScanHint = "SDL_WINDOWS_DETECT_DEVICE_HOTPLUG";

} // namespace

TEST_CASE("the device scan is turned off for a run nobody plays and back on for one a person does", "[window]") {
    // SDL's own setting is the environment's: cleared, so the check does not depend on who runs it.
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), kScanHint);
    // SDL scans by default.
    coney::platform::setDeviceHotplugScan(true);
    CHECK(SDL_GetHintBoolean(kScanHint, true));
    CHECK(SDL_GetHint(kScanHint) == nullptr);

    coney::platform::setDeviceHotplugScan(false);
    CHECK_FALSE(SDL_GetHintBoolean(kScanHint, true));

    // A later run in the same process that a person plays gets SDL's own setting back, not a forced "on".
    coney::platform::setDeviceHotplugScan(true);
    CHECK(SDL_GetHint(kScanHint) == nullptr);
}
