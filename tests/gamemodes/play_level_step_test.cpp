// SPDX-License-Identifier: GPL-3.0-or-later
// The level's core step without the game (docs/guides/network-architecture.md#sub-presentation): PlayLevelSimulation's
// update() runs on a synthetic sandbox with a synthetic character and no presentation, and calls the presentation's
// hooks where their parts of the step fall. Nothing here comes from the game (LEGAL.md, "No game data").
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "characters/character_assets.h"
#include "characters/character_model.h"
#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/play_level_simulation.h"
#include "human/player.h"
#include "sandbox/sandbox_ground.h"
#include "sandbox/sandbox_layout.h"
#include "sandbox/sandbox_world.h"
#include "support/character_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/fixtures.h"
#include "support/human_fixtures.h"

namespace {

using coney::FrameTime;
using coney::GameModeStack;
using coney::PlayLevelSimulation;

// A yard to stand in: a 40 m floor whose top is at z = 0, the player's spawn above its middle, and one of Coney's
// passive targets 3 m ahead of him.
constexpr std::string_view kYard = R"(
title yard
box at=0,0,-0.5 size=40,40,0.5
spawn start at=0,0,1.5 heading=0
target dummy at=0,3,0 heading=180 health=900
)";

// A disc image whose WAD holds one text file: nothing the simulation looks for is on it, which is all it asks of the
// disc until a script names a clip.
std::vector<std::byte> emptyDisc() {
    coney::test::Bytes text;
    text.text("-- nothing to load");
    coney::test::Bytes dir;
    dir.header(1, 0, 0, 0); // one entry, then the header's padding
    dir.u32(0).u32(static_cast<std::uint32_t>(text.size())).u32(coney::nameHash("./ee_files/unused.lua"));
    coney::test::Bytes wad;
    wad.append(text.span());
    wad.padTo(2048);
    return coney::test::buildIso({{"WARRIORS.DIR;1", dir.data()}, {"WARRIORS.WAD;1", wad.data()}});
}

// The synthetic character (tests/support/character_fixtures.h's clump, the locomotion and combat clips, their ranges).
std::unique_ptr<coney::human::PlayerCharacter> syntheticCharacter() {
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    coney::characters::CharacterAssets assets;
    assets.name = "synthetic";
    assets.model = std::move(*model);
    assets.data = coney::test::locomotionData(coney::test::fightClips());
    return coney::human::PlayerCharacter::fromParts(std::move(assets), coney::test::locomotionData(),
                                                    coney::test::fightRanges());
}

// The yard's ground, which must build.
std::unique_ptr<coney::sandbox::SandboxGround> yardGround() {
    auto layout = coney::sandbox::parseSandboxLayout(kYard);
    REQUIRE(layout.has_value());
    auto world = coney::sandbox::SandboxWorld::build(std::move(*layout));
    REQUIRE(world.has_value());
    auto ground = coney::sandbox::SandboxGround::create(std::move(*world), std::nullopt);
    REQUIRE(ground.has_value());
    return std::move(*ground);
}

// The simulation with no presentation: only the hooks every mode must answer, each doing nothing, and a log of the
// presentation hooks the step calls, in order.
class Headless final : public PlayLevelSimulation {
  public:
    Headless(const coney::io::Wad& wad, bool frozen = false)
        : PlayLevelSimulation(
              wad, yardGround(), syntheticCharacter(), [](std::string_view) {}, "synthetic", {},
              coney::human::kPlayerType, true),
          m_frozen(frozen) {
        // The AI as the platform's mode makes it for a level without scripts: the layout's fighters (none here).
        makePlanner();
        makeLayoutFighters({});
    }

    // A mode id from Coney's own range.
    [[nodiscard]] std::uint32_t id() const override { return 0x1feU; }
    // A script's teleport: none come without scripts.
    void teleportPlayer(const coney::world_objects::Placement& /*placement*/,
                        coney::world_objects::TeleportMotion /*motion*/) override {}

    std::vector<std::string> calls; // the presentation hooks called, in order

  protected:
    // No scene holds anyone, so a placement never asks.
    [[nodiscard]] const coney::raycast::CollisionMesh* placeGround(double /*human*/) const override { return nullptr; }
    // Nothing loads without the disc.
    [[nodiscard]] std::expected<std::unique_ptr<coney::human::PlayerCharacter>, coney::Error>
    loadCastCharacter(std::string_view /*model*/) override {
        return coney::fail(coney::ErrorCode::NotFound, "no disc");
    }
    // Nothing is drawn.
    void humanMade(const coney::human::Human& /*human*/, const std::string& /*model*/) override {}
    // Nothing is drawn.
    void changePlayerModel(const std::string& /*model*/) override {}

    // The presentation hooks, logged and doing nothing more (stepScenery() keeps the default camera).
    bool stepFrozenWorld(const FrameTime& /*frame*/) override {
        calls.emplace_back("frozen");
        return m_frozen;
    }
    void traceStep() override { calls.emplace_back("trace"); }
    void stepScenes(std::uint64_t /*nowMs*/, std::uint16_t /*buttons*/) override { calls.emplace_back("scenes"); }
    StepView stepScenery(const FrameTime& frame) override {
        calls.emplace_back("scenery");
        return PlayLevelSimulation::stepScenery(frame);
    }
    void stepGlints(int /*ticks*/) override { calls.emplace_back("glints"); }
    void recordDrawnPoses() override { calls.emplace_back("poses"); }
    void hudStepped() override { calls.emplace_back("hud"); }
    void stepRings(const coney::Pad& /*pad*/, std::uint64_t /*nowMs*/) override { calls.emplace_back("rings"); }
    void stepEnded(const FrameTime& /*frame*/) override { calls.emplace_back("ended"); }

  private:
    bool m_frozen;
};

// The synthetic disc's WAD, opened from a file in `dir`.
coney::io::Wad openEmptyWad(const coney::test::TempDir& dir) {
    auto disc = coney::io::Disc::open(dir.write("disc.iso", emptyDisc()));
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return std::move(*wad);
}

// Runs `steps` fixed steps of `mode` on `stack` (pushed first), its pads sampled from `input` when given.
void run(GameModeStack& stack, PlayLevelSimulation& mode, std::uint64_t steps, coney::InputSource* input = nullptr) {
    stack.setInput(input);
    stack.push(mode);
    for (std::uint64_t i = 0; i < steps; ++i) {
        stack.samplePads(i);
        stack.step(FrameTime{.index = i,
                             .seconds = 1.0 / 30.0,
                             .gameTicks = (i + 1) * coney::GameTimer::kFixedStepTicks,
                             .stepTicks = coney::GameTimer::kFixedStepTicks});
    }
}

} // namespace

// Expected: docs/guides/network-architecture.md#sub-presentation (Coney's own split: the step needs no presentation)
TEST_CASE("the core step runs a sandbox level with no presentation: the player walks and the HUD steps") {
    const coney::test::TempDir dir;
    const coney::io::Wad wad = openEmptyWad(dir);
    Headless mode(wad);
    // Pad 1's left stick pushed fully forward for the whole run.
    auto events = coney::parseInputScript("0 stick left 0 -100\n");
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    GameModeStack stack;
    run(stack, mode, 60, &input);
    CHECK(mode.stats().frames == 60);
    // Two seconds of a pushed stick move him across the floor, and he stays on it.
    CHECK(mode.stats().travelled > 1.0F);
    CHECK(mode.player().human().position().z > -0.5F);
    // The HUD's update is the core's: its clock is the newest step's game time (60 steps of 1/30 s).
    CHECK(mode.hud()->nowMs() == 2000);
}

// Expected: docs/guides/network-architecture.md#sub-presentation (the order is platform::PlayLevelMode::update()'s
// before the split: the traces after the humans, then the scenes, the scenery, the world objects' poses, the HUD, the
// rings; the glints only with world objects, which a sandbox has none of)
TEST_CASE("the core step calls the presentation's hooks once a step, where their parts of the step fall") {
    const coney::test::TempDir dir;
    const coney::io::Wad wad = openEmptyWad(dir);
    Headless mode(wad);
    GameModeStack stack;
    run(stack, mode, 2);
    const std::vector<std::string> step{"frozen", "trace", "scenes", "scenery", "poses", "hud", "rings", "ended"};
    std::vector<std::string> both = step;
    both.insert(both.end(), step.begin(), step.end());
    CHECK(mode.calls == both);
}

// Expected: docs/guides/network-architecture.md#sub-presentation (`--freeze-world`: a frozen step is the
// presentation's alone, and nothing of the simulation advances)
TEST_CASE("a step the presentation froze advances nothing of the simulation") {
    const coney::test::TempDir dir;
    const coney::io::Wad wad = openEmptyWad(dir);
    Headless mode(wad, true);
    auto events = coney::parseInputScript("0 stick left 0 -100\n");
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    GameModeStack stack;
    const coney::anim::Vec3 start = mode.player().human().position();
    run(stack, mode, 10, &input);
    CHECK(mode.calls == std::vector<std::string>(10, "frozen"));
    CHECK(mode.stats().frames == 0);
    CHECK(mode.player().human().position().x == start.x);
    CHECK(mode.player().human().position().y == start.y);
    CHECK(mode.hud()->nowMs() == 0);
}
