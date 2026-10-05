// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/player_trace.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

// The header's column names, in order.
std::vector<std::string> columns(std::string_view header) {
    std::vector<std::string> names;
    std::string name;
    for (const char c : header) {
        if (c == ',' || c == '\n') {
            names.push_back(name);
            name.clear();
        } else {
            name += c;
        }
    }
    return names;
}

} // namespace

TEST_CASE("the trace header names the columns the original's recorder shares", "[human][trace]") {
    // coney-tools pcsx2 record writes these names for the same quantities (research/traces/fields.toml), and
    // coney-tools trace diff compares the columns the two traces share by name.
    const std::vector<std::string> names = columns(coney::human::traceHeader());
    REQUIRE(names.size() == 30);
    CHECK(names.front() == "step");
    constexpr std::array<std::string_view, 20> kShared{
        "x",      "y",      "z",        "heading",      "speed",     "vz",      "gait",      "clip",         "stamina",
        "cam_x",  "look_x", "wanted_x", "cam_distance", "cam_pitch", "cam_yaw", "band_near", "target_pitch", "command",
        "health", "power"};
    for (const std::string_view shared : kShared) {
        std::size_t found = 0;
        for (const std::string& name : names) {
            found += name == shared ? 1 : 0;
        }
        CHECK(found == 1);
    }
    CHECK(names.back() == "power");
}
