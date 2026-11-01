// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/registry.h"

#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "console/catalog.h"
#include "console/command_spec.h"
#include "core/error.h"

using Catch::Matchers::ContainsSubstring;
using coney::ErrorCode;
using coney::console::Access;
using coney::console::ArgSpec;
using coney::console::ArgType;
using coney::console::BuiltinHandler;
using coney::console::CommandContext;
using coney::console::CommandSpec;
using coney::console::Handler;
using coney::console::kModCommandNameMax;
using coney::console::kModCommandsMax;
using coney::console::registerBuiltins;
using coney::console::Registry;
using Strings = std::vector<std::string>;

// The rules: docs/guides/studio/console.md#catalog (one name, one command), #mod-commands (names) and #access.

namespace {

// A handler that does nothing.
const Handler kNothing = [](CommandContext&) -> std::expected<void, std::string> { return {}; };

// A local player command named `name`.
CommandSpec spec(std::string_view name, Access access = Access::Player, std::string_view available = {}) {
    return CommandSpec{
        .name = name, .group = "Test", .access = access, .summary = "A test command.", .available = available};
}

} // namespace

TEST_CASE("a registered built-in resolves by name") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    Registry registry;
    const auto id = registry.addBuiltin(spec("noclip"), kNothing);
    REQUIRE(id.has_value());
    CHECK(registry.resolve("noclip").value() == *id);
    REQUIRE(registry.find(*id) != nullptr);
    CHECK(registry.find(*id)->spec().name == "noclip");
}

TEST_CASE("a second registration of a built-in's name is refused") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    Registry registry;
    REQUIRE(registry.addBuiltin(spec("help"), kNothing).has_value());
    const auto again = registry.addBuiltin(spec("help"), kNothing);
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(again.error().message, ContainsSubstring("help"));
}

TEST_CASE("a badly spelled name is refused") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    Registry registry;
    CHECK_FALSE(registry.addBuiltin(spec("Help"), kNothing).has_value());
    CHECK_FALSE(registry.addBuiltin(spec("two words"), kNothing).has_value());
    CHECK_FALSE(registry.addBuiltin(spec(""), kNothing).has_value());
    CHECK_FALSE(registry.addBuiltin(spec("9lives"), kNothing).has_value());
    CHECK(registry.addBuiltin(spec("load-checkpoint"), kNothing).has_value());
    CHECK(registry.addBuiltin(spec("fps_cap"), kNothing).has_value());
}

TEST_CASE("a player registry refuses a developer command, a developer registry takes both") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#access
    Registry player(Access::Player);
    CHECK_FALSE(player.addBuiltin(spec("noclip", Access::Developer), kNothing).has_value());
    Registry developer(Access::Developer);
    CHECK(developer.addBuiltin(spec("noclip", Access::Developer), kNothing).has_value());
    CHECK(developer.addBuiltin(spec("help"), kNothing).has_value());
}

TEST_CASE("an unknown name is refused with the closest known one") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    Registry registry;
    REQUIRE(registry.addBuiltin(spec("noclip"), kNothing).has_value());
    REQUIRE(registry.addBuiltin(spec("quit"), kNothing).has_value());
    const auto typo = registry.resolve("noclp");
    REQUIRE_FALSE(typo.has_value());
    CHECK(typo.error().code == ErrorCode::NotFound);
    CHECK(typo.error().message == R"(unknown command "noclp"; did you mean "noclip"? help lists the commands)");
    const auto far = registry.resolve("teleport");
    REQUIRE_FALSE(far.has_value());
    CHECK(far.error().message == R"(unknown command "teleport"; help lists the commands)");
}

TEST_CASE("available hides a command until its feature is built") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    Registry registry;
    const auto id = registry.addBuiltin(spec("mission", Access::Player, "mission-select"), kNothing);
    REQUIRE(id.has_value());
    CHECK_FALSE(registry.resolve("mission").has_value());
    CHECK(registry.find(*id) == nullptr);
    CHECK(registry.names().empty());
    registry.setBuiltFeatures({"mission-select"});
    CHECK(registry.resolve("mission").value() == *id);
    CHECK(registry.names() == Strings{"mission"});
}

TEST_CASE("a mod command can never take a built-in's name") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands
    Registry registry;
    REQUIRE(registry.addBuiltin(spec("quit"), kNothing).has_value());
    const auto clash = registry.addModCommand("yard-brawl", spec("quit"), kNothing);
    REQUIRE_FALSE(clash.has_value());
    CHECK_THAT(clash.error().message, ContainsSubstring("built-in"));
    // Nor may a built-in come after a mod's command of its name.
    REQUIRE(registry.addModCommand("yard-brawl", spec("wave"), kNothing).has_value());
    CHECK_FALSE(registry.addBuiltin(spec("wave"), kNothing).has_value());
}

TEST_CASE("two mods' commands of one name are reached as mod:name, and the bare name is ambiguous") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands
    Registry registry;
    const auto first = registry.addModCommand("yard-brawl", spec("wave"), kNothing);
    REQUIRE(first.has_value());
    CHECK(registry.resolve("wave").value() == *first);
    CHECK(registry.resolve("yard-brawl:wave").value() == *first);
    const auto second = registry.addModCommand("riverside", spec("wave"), kNothing);
    REQUIRE(second.has_value());
    const auto bare = registry.resolve("wave");
    REQUIRE_FALSE(bare.has_value());
    CHECK(bare.error().code == ErrorCode::InvalidArgument);
    CHECK(bare.error().message == R"("wave" is ambiguous: riverside:wave, yard-brawl:wave)");
    CHECK(registry.resolve("riverside:wave").value() == *second);
    CHECK(registry.names() == Strings{"riverside:wave", "yard-brawl:wave"});
    CHECK_FALSE(registry.resolve("other:wave").has_value());
}

TEST_CASE("a mod registering its name again replaces the command and keeps its id") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands
    Registry registry;
    const auto first = registry.addModCommand("yard-brawl", spec("wave"), kNothing);
    const auto again = registry.addModCommand("yard-brawl", spec("wave"), kNothing);
    REQUIRE(first.has_value());
    REQUIRE(again.has_value());
    CHECK(*first == *again);
    CHECK(registry.commands().size() == 1);
}

TEST_CASE("a mod command's name is at most 32 characters, and a mod registers at most 64") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands
    Registry registry;
    CHECK(registry.addModCommand("m", spec(std::string(kModCommandNameMax, 'a')), kNothing).has_value());
    CHECK_FALSE(registry.addModCommand("m", spec(std::string(kModCommandNameMax + 1, 'b')), kNothing).has_value());
    Registry full;
    std::vector<std::string> names;
    for (std::size_t i = 0; i < kModCommandsMax; ++i) {
        names.push_back("c" + std::to_string(i));
    }
    for (const std::string& name : names) {
        REQUIRE(full.addModCommand("m", spec(name), kNothing).has_value());
    }
    const auto over = full.addModCommand("m", spec("one-more"), kNothing);
    REQUIRE_FALSE(over.has_value());
    CHECK_THAT(over.error().message, ContainsSubstring("64"));
    // Another mod has its own allowance.
    CHECK(full.addModCommand("n", spec("one-more"), kNothing).has_value());
}

TEST_CASE("a mod command is at the player access level whatever its spec says") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands
    Registry registry;
    const auto id = registry.addModCommand("m", spec("wave", Access::Developer), kNothing);
    REQUIRE(id.has_value());
    CHECK(registry.find(*id)->spec().access == Access::Player);
}

TEST_CASE("removing a mod removes its commands, and their ids stay missing") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands
    Registry registry;
    const auto id = registry.addModCommand("yard-brawl", spec("wave"), kNothing);
    REQUIRE(registry.addModCommand("riverside", spec("wave"), kNothing).has_value());
    REQUIRE(id.has_value());
    CHECK(registry.removeMod("yard-brawl") == 1);
    CHECK(registry.find(*id) == nullptr);
    CHECK(registry.names() == Strings{"wave"});
    CHECK(registry.removeMod("yard-brawl") == 0);
}

TEST_CASE("registerBuiltins registers the entries with handlers and refuses a handler with no entry") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    constexpr std::array<CommandSpec, 2> kCatalog{CommandSpec{.name = "help", .summary = "Lists."},
                                                  CommandSpec{.name = "quit", .summary = "Ends."}};
    Registry registry;
    const std::array<BuiltinHandler, 1> handlers{BuiltinHandler{"help", kNothing}};
    CHECK(registerBuiltins(registry, kCatalog, handlers).value() == 1);
    CHECK(registry.names() == Strings{"help"});
    Registry other;
    const std::array<BuiltinHandler, 1> stray{BuiltinHandler{"helpp", kNothing}};
    const auto refused = registerBuiltins(other, kCatalog, stray);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("helpp"));
}

TEST_CASE("a mod's own name is a command name, so `mod:name` can reach its commands") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands
    Registry registry;
    CHECK_FALSE(registry.addModCommand("", spec("wave"), kNothing).has_value());
    CHECK_FALSE(registry.addModCommand("yard:brawl", spec("wave"), kNothing).has_value());
    CHECK_FALSE(registry.addModCommand("Yard Brawl", spec("wave"), kNothing).has_value());
    CHECK(registry.addModCommand("yard-brawl", spec("wave"), kNothing).has_value());
}

TEST_CASE("a default that its argument would not take is refused when the command registers") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    constexpr std::array<std::string_view, 2> kChoices{"low", "high"};
    const std::array<ArgSpec, 1> badCount{
        ArgSpec{.name = "count", .type = ArgType::Int, .optional = true, .defaultValue = "many"}};
    const std::array<ArgSpec, 1> badChoice{ArgSpec{
        .name = "level", .type = ArgType::Choice, .optional = true, .defaultValue = "medium", .choices = kChoices}};
    const std::array<ArgSpec, 1> good{ArgSpec{
        .name = "level", .type = ArgType::Choice, .optional = true, .defaultValue = "low", .choices = kChoices}};
    Registry registry;
    CommandSpec command = spec("wave");
    command.args = badCount;
    const auto refused = registry.addBuiltin(command, kNothing);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("count"));
    command.args = badChoice;
    CHECK_FALSE(registry.addModCommand("m", command, kNothing).has_value());
    command.args = good;
    CHECK(registry.addBuiltin(command, kNothing).has_value());
}
