// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/catalog.h"

#include <set>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "console/command_spec.h"
#include "console/registry.h"

using coney::console::Access;
using coney::console::findSpec;
using coney::console::isCommandName;
using coney::console::kBuiltinNameMax;
using coney::console::playerCatalog;
using coney::console::Scope;

// The generated table from src/console/commands.toml (`coney-tools console render-cpp`; CI's `--check` keeps it
// current). These hold the rules the generator also checks, on what the game library actually compiled.

TEST_CASE("the player catalog holds only player commands, each name once and well spelled") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    std::set<std::string_view> names;
    REQUIRE_FALSE(playerCatalog().empty());
    for (const auto& spec : playerCatalog()) {
        INFO(spec.name);
        CHECK(spec.access == Access::Player);
        CHECK(isCommandName(spec.name, kBuiltinNameMax));
        CHECK(names.insert(spec.name).second);
        CHECK_FALSE(spec.summary.empty());
        CHECK_FALSE(spec.group.empty());
        // A required argument never follows an optional one.
        bool optionalSeen = false;
        for (const auto& arg : spec.args) {
            CHECK((arg.optional || !optionalSeen));
            optionalSeen = optionalSeen || arg.optional;
            CHECK((arg.defaultValue.empty() || arg.optional));
        }
    }
}

TEST_CASE("the player catalog has the release set's entries as the design lists them") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set: quitting is local, a mission is the
    // session's and waits for Coney's mission select.
    const auto* quit = findSpec(playerCatalog(), "quit");
    REQUIRE(quit != nullptr);
    CHECK(quit->scope == Scope::Local);
    CHECK(quit->changes);
    const auto* mission = findSpec(playerCatalog(), "mission");
    REQUIRE(mission != nullptr);
    CHECK(mission->scope == Scope::Session);
    CHECK(mission->available == "mission-select");
    REQUIRE(mission->args.size() == 2);
    CHECK(mission->args[1].defaultValue == "1");
    CHECK(findSpec(playerCatalog(), "noclip") == nullptr);
}
