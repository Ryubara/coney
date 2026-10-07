// SPDX-License-Identifier: GPL-3.0-or-later
// The light types (docs/research/script-types.md#light-type-lights): the cross-fade between a light's two colours,
// the strober's 40-tick pulse and the neon light's steady spells and blinks.
#include "effects/light_tasks.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using coney::effects::lightColour;
using coney::effects::LightTask;
using coney::effects::tickLight;

namespace {

// A random draw that always gives `value` (clamped to the draw's top).
coney::effects::LightRandom fixedRandom(std::uint32_t value) {
    return [value](std::uint32_t n) { return value < n ? value : n; };
}

// Runs `ticks` ticks of the light.
void run(LightTask& task, int ticks, const coney::effects::LightRandom& random = fixedRandom(0)) {
    for (int i = 0; i < ticks; ++i) {
        tickLight(task, random);
    }
}

} // namespace

TEST_CASE("a strober is red for its first 30 ticks, then pulses on a 40-tick cycle", "[effects][lights]") {
    LightTask strober = coney::effects::makeStrober();
    CHECK(strober.radius == 10.0F);
    CHECK(strober.lightsWorld);
    CHECK(lightColour(strober).r == 1.0F);
    CHECK(lightColour(strober).g == 0.0F);
    run(strober, 29);
    CHECK(lightColour(strober).r == 1.0F);
    // Step 3 (tick 30) aims at black: halfway down 5 ticks later, black at tick 40 and until tick 60.
    run(strober, 6);
    CHECK_THAT(lightColour(strober).r, WithinAbs(0.5, 1e-6));
    run(strober, 5);
    CHECK(lightColour(strober).r == 0.0F);
    run(strober, 20);
    CHECK(lightColour(strober).r == 0.0F);
    // Step 2 (tick 60) aims at red: halfway up at 65, red at 70, halfway down at 75, black from 80 to 100.
    run(strober, 5);
    CHECK_THAT(lightColour(strober).r, WithinAbs(0.5, 1e-6));
    run(strober, 5);
    CHECK(lightColour(strober).r == 1.0F);
    run(strober, 5);
    CHECK_THAT(lightColour(strober).r, WithinAbs(0.5, 1e-6));
    run(strober, 5);
    CHECK(lightColour(strober).r == 0.0F);
    run(strober, 25); // tick 105: the next cycle's fade up
    CHECK_THAT(lightColour(strober).r, WithinAbs(0.5, 1e-6));
}

TEST_CASE("a strober switched off ends at its next update", "[effects][lights]") {
    LightTask strober = coney::effects::makeStrober();
    run(strober, 3);
    strober.ending = true;
    run(strober, 6);
    CHECK_FALSE(strober.done);
    CHECK(lightColour(strober).r == 1.0F);
    run(strober, 1);
    CHECK(strober.done);
    CHECK(lightColour(strober).r == 0.0F);
}

TEST_CASE("a neon light fades out each second at first, then holds steady between blinks", "[effects][lights]") {
    constexpr std::uint32_t kOrange = 0xffe3aeffU;
    LightTask neon = coney::effects::makeNeonLight(kOrange);
    CHECK(neon.radius == 4.0F);
    CHECK(neon.lightsWorld);
    CHECK(lightColour(neon).g == 227.0F / 255.0F);
    // The first cycle: snapped to the colour each 60 ticks and faded towards black over them.
    run(neon, 30);
    CHECK_THAT(lightColour(neon).g, WithinAbs(227.0 / 255.0 / 2.0, 1e-6));
    run(neon, 30);
    CHECK(lightColour(neon).g == 227.0F / 255.0F);
    // The 16th update passes the threshold: the off spell fades to black over 5 ticks.
    run(neon, 60 * 15);
    CHECK(neon.offSpell);
    CHECK(neon.interval == 5);
    // A blink: black at once, fading up over 15 ticks; with a draw of 7 the counter is 8, so a second blink follows.
    neon = coney::effects::makeNeonLight(kOrange);
    run(neon, 60 * 16 + 5, fixedRandom(7));
    CHECK(neon.offSpell);
    CHECK(neon.counter == 8);
    CHECK(lightColour(neon).r == 0.0F);
    run(neon, 15, fixedRandom(7));
    CHECK(neon.counter == 0);
    CHECK_FALSE(neon.offSpell);
    CHECK(neon.interval == 30);
    CHECK(lightColour(neon).r == 0.0F);
    // The last fade up takes 30 ticks; steady from then on, the colour held.
    run(neon, 15, fixedRandom(7));
    CHECK_THAT(lightColour(neon).g, WithinAbs(227.0 / 255.0 / 2.0, 1e-6));
    run(neon, 15 + 30 * 10, fixedRandom(7));
    CHECK_FALSE(neon.offSpell);
    CHECK(lightColour(neon).g == 227.0F / 255.0F);
    CHECK(lightColour(neon).b == 174.0F / 255.0F);
}
