// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/light_tasks.h"

#include <algorithm>

namespace coney::effects {

namespace {

// The strober's numbers (docs/research/script-types.md#strober).
constexpr std::uint32_t kStroberRed = 0xff0000ffU;
constexpr std::uint32_t kBlack = 0x000000ffU;
constexpr float kStroberRadius = 10.0F;
constexpr int kStroberInterval = 10;
constexpr int kStroberSteps = 4;
constexpr int kStroberRedStep = 2;
constexpr int kStroberBlackStep = 3;

// The neon light's numbers (docs/research/script-types.md#neon-signs).
constexpr float kNeonRadius = 4.0F;
constexpr int kNeonFirstInterval = 60;
constexpr int kNeonSteadyInterval = 30;
constexpr int kNeonFadeOutInterval = 5;
constexpr int kNeonBlinkInterval = 15;
constexpr int kNeonThreshold = 15;
// The colour `LightTask_Init` leaves as the target: 0 (black, alpha 0).
constexpr std::uint32_t kNeonBlack = 0;

// `Strober_Update`: steps 1-4 in turn; step 2 aims at red, step 3 at black, 4 and 1 leave the target.
// @orig 0x003e8518 Strober_Update (unknown)
void updateStrober(LightTask& task) {
    task.counter = (task.counter % kStroberSteps) + 1;
    if (task.counter == kStroberRedStep) {
        task.to = kStroberRed;
    } else if (task.counter == kStroberBlackStep) {
        task.to = kBlack;
    }
}

// `SubNeonLight_Update`: steady, the light snaps to the sign's colour each update and the counter climbs; past the
// threshold an off spell fades it out over 5 ticks, then blinks it (black, fading back up over 15 ticks) until the
// counter, growing by 1 + Random_Int(interval) a blink, passes the threshold again, ending in a 30-tick fade up.
// @orig 0x003e03d8 SubNeonLight_Update (unknown)
void updateNeon(LightTask& task, const LightRandom& random) {
    if (!task.offSpell) {
        task.from = task.colour;
        ++task.counter;
        if (task.counter > kNeonThreshold) {
            // The off spell starts with a fade to black.
            task.offSpell = true;
            task.counter = 0;
            task.to = kNeonBlack;
            task.interval = kNeonFadeOutInterval;
        }
        return;
    }
    // A blink: black at once, fading back up to the colour over the interval.
    task.from = kNeonBlack;
    task.to = task.colour;
    task.counter += 1 + static_cast<int>(random(static_cast<std::uint32_t>(kNeonBlinkInterval)));
    if (task.counter > kNeonThreshold) {
        // Back to steady, with a last slow fade up.
        task.offSpell = false;
        task.counter = 0;
        task.interval = kNeonSteadyInterval;
    } else {
        task.interval = kNeonBlinkInterval;
    }
}

// One channel (0-1) of `0xRRGGBBAA` at bit `shift`.
float channel(std::uint32_t colour, unsigned shift) {
    constexpr float kMax = 255.0F;
    return static_cast<float>((colour >> shift) & 0xffU) / kMax;
}

} // namespace

LightTask makeStrober() {
    LightTask task;
    task.kind = LightKind::Strober;
    task.from = kStroberRed;
    task.to = kStroberRed;
    task.radius = kStroberRadius;
    task.lightsWorld = true;
    task.interval = kStroberInterval;
    return task;
}

LightTask makeNeonLight(std::uint32_t colour) {
    LightTask task;
    task.kind = LightKind::Neon;
    task.colour = colour;
    task.from = colour;
    task.to = kNeonBlack;
    task.radius = kNeonRadius;
    task.lightsWorld = true;
    task.interval = kNeonFirstInterval;
    return task;
}

void tickLight(LightTask& task, const LightRandom& random) {
    if (task.done) {
        return;
    }
    ++task.since;
    if (task.since < task.interval) {
        return;
    }
    // The update: a cleared on flag ends the task; otherwise the target is reached and the type picks the next.
    task.since = 0;
    if (task.ending) {
        task.done = true;
        return;
    }
    task.from = task.to;
    switch (task.kind) {
    case LightKind::Strober:
        updateStrober(task);
        break;
    case LightKind::Neon:
        updateNeon(task, random);
        break;
    }
}

LightColourNow lightColour(const LightTask& task) {
    if (task.done) {
        return {};
    }
    const float t =
        task.interval > 0 ? std::min(1.0F, static_cast<float>(task.since) / static_cast<float>(task.interval)) : 1.0F;
    // Linear in each channel from the colour at the last update to the target.
    const auto mix = [&](unsigned shift) {
        const float a = channel(task.from, shift);
        return a + ((channel(task.to, shift) - a) * t);
    };
    constexpr unsigned kRed = 24;
    constexpr unsigned kGreen = 16;
    constexpr unsigned kBlue = 8;
    return LightColourNow{mix(kRed), mix(kGreen), mix(kBlue)};
}

} // namespace coney::effects
