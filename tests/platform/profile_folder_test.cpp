// SPDX-License-Identifier: GPL-3.0-or-later
// Where the player profiles are kept (docs/research/save.md#coney).
#include "platform/profile_folder.h"

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("the profile folder is the one named, and none in test mode", "[profile_folder]") {
    coney::Options options;
    options.headless = true;
    CHECK_FALSE(coney::platform::profileFolder(options).has_value());
    options.profilesDir = "saves";
    CHECK(coney::platform::profileFolder(options) == std::filesystem::path("saves"));
}
