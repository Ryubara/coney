// SPDX-License-Identifier: GPL-3.0-or-later
// The engine limits preset as the game mode owns it (docs/guides/running-options.md#engine-limits): the start-up flow
// hands it to gameplay, and gameplay to every part of a level that applies a lifted limit (the brains and their gangs,
// the trigger spheres, the volume boxes), before a level is entered and after.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brains.h"
#include "core/engine_limits.h"
#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "support/lua_fixtures.h"
#include "support/recording_device.h"

namespace {

// The fewest scripts a level loads with: empty preloads, config_preload3.lua listing level1, and empty global.lua and
// level1.lua.
std::map<std::string, std::vector<std::byte>, std::less<>> plainScripts() {
    using coney::test::LuaAsm;
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    for (const char* empty :
         {"enum_preload.lua", "config_preload.lua", "config_preload2.lua", "global.lua", "level1.lua"}) {
        files[empty] = coney::test::luaChunk(LuaAsm().end());
    }
    LuaAsm levels;
    levels.getGlobal("CfgLevelName").pushInt(1).pushString("level1").pushString("").pushString("level1");
    levels.pushString("").pushInt(1).call(6);
    files["config_preload3.lua"] = coney::test::luaChunk(levels.end());
    return files;
}

// A loaded level that does nothing.
class EmptyLevel final : public coney::GameMode {
  public:
    [[nodiscard]] std::uint32_t id() const override { return 0x1ff; }
    coney::ModeResult update(coney::GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        return coney::ModeResult::Stay;
    }
};

} // namespace

TEST_CASE("gameplay hands its engine limits to the brains, the trigger spheres and the volume boxes",
          "[gameplay][engine-limits]") {
    // Expected: unverified, #1591 (Coney's own wiring of the engine limits preset:
    // docs/guides/running-options.md#engine-limits)
    const auto files = plainScripts();
    const coney::script::ScriptSource source =
        [&files](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
        const auto found = files.find(name);
        if (found == files.end()) {
            return coney::fail(coney::ErrorCode::NotFound, "no such script");
        }
        return found->second;
    };
    coney::LevelScripts scripts(source, "level1", 1, [](std::string_view) {});
    REQUIRE(scripts.context().boxes != nullptr);
    coney::test::RecordingDevice device;
    coney::GameplayMode gameplay(
        device, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(), scripts.flags(),
        scripts.recorded(),
        [](const coney::LevelStart& /*start*/, const coney::ScriptedCast& /*cast*/)
            -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> { return std::make_unique<EmptyLevel>(); },
        [](std::string_view) {});
    // Coney's own rule until told otherwise.
    CHECK(gameplay.engineLimits() == coney::EngineLimits::Coney);
    CHECK(gameplay.spheres().engineLimits() == coney::EngineLimits::Coney);

    // Set before a level: the level's brains are made with it.
    gameplay.setEngineLimits(coney::EngineLimits::Original);
    CHECK(gameplay.engineLimits() == coney::EngineLimits::Original);
    CHECK(gameplay.spheres().engineLimits() == coney::EngineLimits::Original);
    CHECK(scripts.context().boxes->engineLimits() == coney::EngineLimits::Original);
    gameplay.setLevel("level1");
    gameplay.enter();
    const coney::ai::Brains* brains = gameplay.brains();
    REQUIRE(brains != nullptr);
    CHECK(brains->engineLimits() == coney::EngineLimits::Original);
    CHECK(brains->gangs().engineLimits() == coney::EngineLimits::Original);

    // Set again in a level (a test may), it reaches the level's brains too.
    gameplay.setEngineLimits(coney::EngineLimits::Coney);
    CHECK(brains->engineLimits() == coney::EngineLimits::Coney);
    CHECK(gameplay.spheres().engineLimits() == coney::EngineLimits::Coney);
    CHECK(scripts.context().boxes->engineLimits() == coney::EngineLimits::Coney);
    gameplay.exit();
}
