// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/legal_screen_mode.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/idle_mode.h"
#include "graphics/screen.h"
#include "support/recording_device.h"

using coney::GameModeStack;
using coney::GameTimer;
using coney::Language;
using coney::LegalScreenMode;
using coney::LegalScreenSettings;
using coney::graphics::SpriteSheet;
using coney::graphics::UvRect;
using coney::test::FakeTexture;
using coney::test::RecordingDevice;

namespace {

// A loader that returns a one-rectangle sheet over a fake texture and records the names it was asked for.
struct FakeLoader {
    std::shared_ptr<FakeTexture> texture = std::make_shared<FakeTexture>(512, 512);
    std::vector<std::string> requested;

    LegalScreenMode::SheetLoader loader() {
        return [this](std::string_view name) -> std::expected<SpriteSheet, coney::Error> {
            requested.emplace_back(name);
            SpriteSheet sheet;
            sheet.page.rects.push_back(UvRect{0.0F, 0.0F, 1.0F, 0.75F});
            sheet.texture = texture;
            return sheet;
        };
    }
};

// A log that keeps its lines.
struct Log {
    std::vector<std::string> lines;
    std::function<void(std::string_view)> sink() {
        return [this](std::string_view line) { lines.emplace_back(line); };
    }
};

} // namespace

TEST_CASE("the legal screen's resource name follows language, aspect and the flag 0x02", "[legal_screen]") {
    CHECK(coney::legalScreenResourceName({}) == "legal_screen");
    CHECK(coney::legalScreenResourceName({Language::Spanish, false, false}) == "legal_screen_sp");
    CHECK(coney::legalScreenResourceName({Language::French, false, false}) == "legal_screen_fr");
    CHECK(coney::legalScreenResourceName({Language::Italian, false, false}) == "legal_screen_it");
    CHECK(coney::legalScreenResourceName({Language::German, false, false}) == "legal_screen_ge");
    CHECK(coney::legalScreenResourceName({Language::English, true, false}) == "legal_screen_w");
    CHECK(coney::legalScreenResourceName({Language::German, true, false}) == "legal_screen_w_ge");
    CHECK(coney::legalScreenResourceName({Language::English, false, true}) == "legal_screen_euro");
    CHECK(coney::legalScreenResourceName({Language::English, true, true}) == "legal_screen_euro");
    // The flag only changes the English screen.
    CHECK(coney::legalScreenResourceName({Language::French, false, true}) == "legal_screen_fr");
}

TEST_CASE("a resource's WAD file name is the decimal CRC-32 of its name", "[legal_screen]") {
    // The value docs/research/graphics.md#first-screen gives for the legal screen.
    CHECK(coney::resourceFileName("legal_screen") == "863681355");
}

TEST_CASE("the legal screen holds exactly 5,000 ms of game time, then leaves", "[legal_screen]") {
    RecordingDevice device;
    FakeLoader loader;
    Log log;
    coney::IdleMode below;
    LegalScreenMode legal(device, loader.loader(), {}, log.sink());
    GameModeStack stack;
    stack.push(below);
    stack.push(legal);
    GameTimer timer;
    timer.setFixedStep(true);

    // 5,000 ms is 150 steps of 1/30 s: the legal screen runs 150 frames and the mode below takes over on the 151st.
    stack.runUntilEmpty(timer, {}, std::uint64_t{149});
    CHECK(stack.topId() == LegalScreenMode::kId);
    stack.runUntilEmpty(timer, {}, std::uint64_t{1});
    CHECK(stack.topId() == coney::IdleMode::kId);
    CHECK(loader.requested == std::vector<std::string>{"legal_screen"});
    CHECK(log.lines.empty());
    // The picture is released when the mode leaves.
    CHECK_FALSE(legal.sheet().has_value());
    CHECK(loader.texture.use_count() == 1);
}

TEST_CASE("the legal screen draws its picture over the whole screen on black every frame", "[legal_screen]") {
    RecordingDevice device;
    FakeLoader loader;
    Log log;
    LegalScreenMode legal(device, loader.loader(), {}, log.sink());
    GameModeStack stack;
    stack.push(legal);
    GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, std::uint64_t{2});

    CHECK(device.calls == std::vector<std::string>{"begin", "draw", "present", "begin", "draw", "present"});
    REQUIRE(device.clears.size() == 2);
    CHECK(device.clears[0] == coney::graphics::kBlack);
    REQUIRE(device.draws.size() == 2);
    const coney::test::RecordedDraw& draw = device.draws[0];
    CHECK(draw.texture == loader.texture.get());
    REQUIRE(draw.quads.size() == 1);
    const coney::graphics::LogicalQuad& quad = draw.quads[0];
    CHECK(quad.x == 0.0F);
    CHECK(quad.y == 0.0F);
    CHECK(quad.width == coney::graphics::kLogicalWidth);
    CHECK(quad.height == coney::graphics::kLogicalHeight);
    CHECK(quad.uv == UvRect{0.0F, 0.0F, 1.0F, 0.75F});
    CHECK(quad.colour == coney::graphics::kWhite);
}

TEST_CASE("a legal screen that cannot load is logged and stays black for the hold", "[legal_screen]") {
    RecordingDevice device;
    Log log;
    const LegalScreenMode::SheetLoader failing = [](std::string_view) -> std::expected<SpriteSheet, coney::Error> {
        return coney::fail(coney::ErrorCode::NotFound, "not on this disc");
    };
    LegalScreenMode legal(device, failing, {Language::German, true, false}, log.sink());
    GameModeStack stack;
    stack.push(legal);
    GameTimer timer;
    timer.setFixedStep(true);
    CHECK(stack.runUntilEmpty(timer, {}, std::uint64_t{1000}) == 150);
    CHECK(stack.empty());
    REQUIRE(log.lines.size() == 1);
    CHECK(log.lines[0].find("legal_screen_w_ge") != std::string::npos);
    CHECK(device.draws.empty());
    CHECK(device.clears.size() == 150);
}
