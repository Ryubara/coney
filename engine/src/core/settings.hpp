#pragma once

#include <cstdint>
#include <string>

#include "core/log.hpp"

namespace coney::core {

// The rate the game's logic runs at: 30 frames a second (research/pages/number-conventions/page.md, game frame rate;
// research/pages/start-up/page.md, Clocks and frame rate).
inline constexpr int kGameTicksPerSecond = 30;

struct Colour {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct WindowSettings {
    std::string title = "Coney";
    int width = 1280;
    int height = 720;
};

struct LoopSettings {
    int ticks_per_second = kGameTicksPerSecond;
    // Most logic ticks run in one frame; a stall longer than this many ticks is dropped rather than caught up.
    int max_catch_up_ticks = 5;
};

struct LogSettings {
    LogLevel level = LogLevel::Info;
    std::string file; // empty: standard error only
};

struct Settings {
    WindowSettings window;
    LoopSettings loop;
    LogSettings log;
    Colour clear_colour{0.08f, 0.06f, 0.12f, 1.0f};
};

} // namespace coney::core
