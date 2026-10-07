// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the third story mission's tagging plays end to end
// (docs/research/crimes.md#tagging): in `level87` at checkpoint 1, player 1 put at the first tag spot sees the spot's
// action prompt, triangle hands it to the script, which starts the spray (the intro clip where he stands, then the
// stick game as the loop starts), and tracing the pattern with the left
// stick finishes the tag; the level's start callback runs without a script error
// (docs/research/crimes.md#tag-callbacks). It runs only when the environment variable CONEY_DISC names the disc and
// skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "hud/hud.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scripting/config_strings.h"
#include "scripting/message_handlers.h"
#include "scripting/script_system.h"
#include "warriors/tag_game.h"
#include "world/sector_budget.h"

namespace {

// The disc named by CONEY_DISC, opened; nothing when it is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<coney::io::Wad>(std::move(*wad)) : std::nullopt;
}

// A pad the test drives frame by frame: the buttons held and the left stick in [-1, 1] with y up.
class DrivenPad final : public coney::InputSource {
  public:
    std::uint16_t buttons = 0;
    float leftX = 0.0F;
    float leftY = 0.0F;

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        pad.buttons = buttons;
        pad.sticks[2] = raw(leftX);
        pad.sticks[3] = raw(-leftY);
        return samples;
    }

  private:
    // The raw byte libpad would give for `value` (pad::stickValue()'s inverse, outside its dead zone).
    static std::uint8_t raw(float value) {
        constexpr float kSpan = 95.0F;
        if (value > 0.0F) {
            return static_cast<std::uint8_t>(std::lround(160.0F + (std::min(value, 1.0F) * kSpan)));
        }
        if (value < 0.0F) {
            return static_cast<std::uint8_t>(std::lround(95.0F + (std::max(value, -1.0F) * kSpan)));
        }
        return coney::pad::kStickCentre;
    }
};

} // namespace

TEST_CASE("the disc's level87 offers its first tag spot, starts the stick game and reports the finished tag",
          "[disc][story][tagging]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // level87 at checkpoint 1 as `--play-level level87 --checkpoint 1` loads it.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level87", 1, print, options);
    coney::GameplayMode::LevelLoader loader =
        [&renderer, &wad, &budget, &scripts,
         &print](const coney::LevelStart& start,
                 const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
        std::optional<coney::human::PlayerStart> playerStart;
        coney::platform::PlayerSetup setup;
        setup.ai = coney::ai::aiConfigFrom(scripts.recorded());
        setup.types = coney::characters::CharacterTypes::fromRecorded(scripts.recorded());
        if (start.player) {
            const coney::HumanCreation& player = *start.player;
            const std::array<float, 3> p =
                player.teleported ? player.teleported->position : player.position.value_or(std::array<float, 3>{});
            playerStart = coney::human::PlayerStart{
                .position = coney::anim::Vec3{p[0], p[1], p[2]},
                .headingDegrees = player.teleported ? player.teleported->headingDegrees : player.headingDegrees};
            setup.model = player.model.empty() ? std::string(coney::human::kPlayerModel) : player.model;
            setup.type = player.type;
            setup.snapToGround = !player.teleported;
        }
        auto mode = coney::platform::PlayLevelMode::create(renderer, *wad, start.level, budget, print, playerStart,
                                                           setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), std::move(loader), print);
    gameplay.setLevel("level87");
    DrivenPad pad;
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    const auto run = [&stack, &timer](std::uint64_t frames) { stack.runUntilEmpty(timer, {}, frames); };

    // The opening scene plays out until the script gives the first tag spot its prompt (SetMsgHandlerEx).
    const coney::script::MessageHandlers* messages = scripts.context().messages;
    REQUIRE(messages != nullptr);
    constexpr int kWaitSteps = 120;
    constexpr std::uint64_t kWaitFrames = 30;
    for (int i = 0; i < kWaitSteps && messages->prompts().empty(); ++i) {
        run(kWaitFrames);
    }
    REQUIRE_FALSE(messages->prompts().empty());
    auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);
    // The scene that hands out the job keeps the pad until it ends.
    for (int i = 0; i < kWaitSteps && !play->player().padControlled(); ++i) {
        run(kWaitFrames);
    }
    REQUIRE(play->player().padControlled());
    const double spot = messages->prompts().begin()->first;
    const std::string promptText = messages->prompts().begin()->second;
    const coney::world_objects::WorldFlag* flag = scripts.flags().find(spot);
    REQUIRE(flag != nullptr);

    // Player 1 put at the spot (on the raised ground under it): its text becomes his action prompt.
    play->teleportPlayer(coney::world_objects::Placement{.position = flag->position, .headingDegrees = 0.0F});
    run(kWaitFrames);
    REQUIRE(gameplay.pickups() != nullptr);
    CHECK(gameplay.pickups()->actionObject(play->player().human().position()).has_value());
    CHECK(scripts.hud().actionPrompt(0) == promptText);

    // Triangle: the spot's handler takes the press and the script starts the stick game (HuTag).
    pad.buttons = coney::pad::kTriangle;
    run(2);
    pad.buttons = 0;
    run(2);
    // He plays the spray's intro (334) where he stands; the stick game goes live as its loop (335) starts.
    CHECK(gameplay.tagSession() == nullptr);
    CHECK(play->player().human().animator().animId() == 334);
    constexpr int kIntroFrames = 150;
    for (int i = 0; i < kIntroFrames && gameplay.tagSession() == nullptr; ++i) {
        run(1);
    }
    CHECK(play->player().human().animator().animId() == 335);
    const coney::TagSession* session = gameplay.tagSession();
    REQUIRE(session != nullptr);
    CHECK(scripts.hud().actionPrompt(0).empty());
    // The level's start callback ran with the tagger, the tag and the flag, and failed nowhere.
    CHECK(scripts.scripts().errors() == 0);
    const std::size_t pathPoints = session->game().path().size();
    const double tag = session->tag();

    // The left stick steers the cursor along the pattern, a few points ahead of the progress, until the game ends.
    constexpr int kMaxFrames = 30 * 120;
    constexpr std::size_t kLead = 3;
    int frames = 0;
    while (gameplay.tagSession() != nullptr && frames < kMaxFrames) {
        const coney::TagGame& game = gameplay.tagSession()->game();
        const std::vector<coney::TagCell>& path = game.path();
        const coney::TagCell target = path[std::min(game.progress() + kLead, path.size() - 1)];
        const float dx = static_cast<float>(target.x) - game.cursorX();
        const float dy = static_cast<float>(target.y) - game.cursorY();
        const float length = std::hypot(dx, dy);
        pad.leftX = length > 0.5F ? dx / length : 0.0F;
        pad.leftY = length > 0.5F ? dy / length : 0.0F;
        run(1);
        ++frames;
    }
    pad.leftX = 0.0F;
    pad.leftY = 0.0F;
    run(2);
    REQUIRE(gameplay.tagSession() == nullptr);

    // Finished: the spot is fully painted, the script took it off the list of spots to tag, and no script failed.
    bool finished = false;
    for (const std::string& line : log) {
        if (line.starts_with("tag:") && line.ends_with("finished\n") && !line.ends_with("unfinished\n")) {
            finished = true;
        }
        if (line.starts_with("script error")) {
            UNSCOPED_INFO(line);
        }
    }
    CHECK(finished);
    const coney::world_objects::TagSpot* sprayed = gameplay.tagSpots().find(tag);
    REQUIRE(sprayed != nullptr);
    CHECK(sprayed->fraction == 1.0F);
    CHECK_FALSE(messages->prompts().contains(spot));
    CHECK(scripts.scripts().errors() == 0);
    std::printf("  level87 tagging: %zu path points, finished in %d frames, %zu prompts left\n", pathPoints, frames,
                messages->prompts().size());
}
