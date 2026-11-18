// SPDX-License-Identifier: GPL-3.0-or-later
// What the console takes from the running game (docs/guides/console.md#in-the-game): the settings a player has over
// the parts that exist, and the links the needs, the version and the level come from. The parts are the real ones in
// test mode (a frame pacer, a frame clock, a NULL renderer, an offline sound output), with a session's facts given as
// the entry points would read them.
#include "platform/game_console_links.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "console/command_spec.h"
#include "core/display_preset.h"
#include "core/frame_clock.h"
#include "core/renderer_choice.h"
#include "graphics/render_device.h"
#include "install/installer.h"
#include "platform/audio_output.h"
#include "platform/frame_pacer.h"
#include "platform/render_engine.h"

using coney::FrameClock;
using coney::FramePacing;
using coney::RendererKind;
using coney::console::Need;
using coney::platform::AudioOutput;
using coney::platform::AudioSink;
using coney::platform::FramePacer;
using coney::platform::GameConsoleLinks;
using coney::platform::GameConsoleSources;
using coney::platform::GameSetting;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::SessionFacts;

namespace {

// The setting called `name` among `settings`; null when there is none.
GameSetting* find(std::vector<GameSetting>& settings, std::string_view name) {
    for (GameSetting& setting : settings) {
        if (setting.name == name) {
            return &setting;
        }
    }
    return nullptr;
}

// The names of `settings`, in order.
std::vector<std::string> namesOf(const std::vector<GameSetting>& settings) {
    std::vector<std::string> names;
    for (const GameSetting& setting : settings) {
        names.push_back(setting.name);
    }
    return names;
}

// Puts the display preset back to *PS2* (where the settings start) when a test ends, as the setting changes a global.
struct PresetRestore {
    PresetRestore() = default;
    PresetRestore(const PresetRestore&) = delete;
    PresetRestore& operator=(const PresetRestore&) = delete;
    PresetRestore(PresetRestore&&) = delete;
    PresetRestore& operator=(PresetRestore&&) = delete;
    ~PresetRestore() { coney::displaySettings().choose(coney::DisplayPreset::Ps2); }
};

} // namespace

TEST_CASE("the frame cap setting sets the pacer, the renderer's cap and the clock's pacing") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (set); 30 is the original's one step a
    // frame.
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    FramePacer pacer(60);
    FrameClock clock(FramePacing::Interpolated);
    GameConsoleSources sources;
    sources.renderer = engine->get();
    sources.pacer = &pacer;
    sources.clock = &clock;
    auto settings = coney::platform::playerSettings(sources);
    GameSetting* cap = find(settings, "fps_cap");
    REQUIRE(cap != nullptr);
    CHECK(cap->get() == "60");

    // Any cap but 30 blends between steps; the pacer holds the cap.
    CHECK(cap->set("144").value() == "144");
    CHECK(pacer.cap() == 144);
    CHECK((*engine)->fpsCap() == 144);
    CHECK(clock.pacing() == FramePacing::Interpolated);
    // 30 is one step a frame, which is lockstep.
    CHECK(cap->set("30").value() == "30");
    CHECK(pacer.cap() == 30);
    CHECK((*engine)->fpsCap() == 30);
    CHECK(clock.pacing() == FramePacing::Lockstep);
    CHECK(cap->get() == "30");
    // 0 is no cap, and blends again.
    CHECK(cap->set("0").value() == "0");
    CHECK(pacer.cap() == 0);
    CHECK((*engine)->fpsCap() == 0);
    CHECK(clock.pacing() == FramePacing::Interpolated);
    // A cap out of range changes nothing.
    CHECK_FALSE(cap->set("1001").has_value());
    CHECK(pacer.cap() == 0);
    CHECK((*engine)->fpsCap() == 0);
}

TEST_CASE("the frame cap setting works with no renderer and no clock, and is absent without a frame pacer") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (a part that does not exist leaves its
    // setting out)
    FramePacer pacer(60);
    GameConsoleSources onlyPacer;
    onlyPacer.pacer = &pacer;
    auto settings = coney::platform::playerSettings(onlyPacer);
    GameSetting* cap = find(settings, "fps_cap");
    REQUIRE(cap != nullptr);
    CHECK(cap->set("90").value() == "90");
    CHECK(pacer.cap() == 90);

    // Test mode has no pacer, and so no cap to set.
    auto none = coney::platform::playerSettings(GameConsoleSources{});
    CHECK(find(none, "fps_cap") == nullptr);
}

TEST_CASE("the master volume setting is the mixer's master volume and no bus's") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (volume is the master volume, 0 to 1)
    auto audio = AudioOutput::start(AudioSink::Offline);
    REQUIRE(audio.has_value());
    GameConsoleSources sources;
    sources.audio = audio->get();
    auto settings = coney::platform::playerSettings(sources);
    GameSetting* volume = find(settings, "volume");
    REQUIRE(volume != nullptr);
    const float bus = (*audio)->volume(1);
    CHECK(volume->get() == "1");
    CHECK(volume->set("0.25").value() == "0.25");
    CHECK((*audio)->volume(0) == 0.25F);
    CHECK((*audio)->volume(1) == bus);
    CHECK(volume->get() == "0.25");
    CHECK_FALSE(volume->set("2").has_value());
    CHECK((*audio)->volume(0) == 0.25F);

    // Without sound there is no volume to set.
    auto none = coney::platform::playerSettings(GameConsoleSources{});
    CHECK(find(none, "volume") == nullptr);
}

TEST_CASE("the display preset setting chooses the display's preset by its name") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set; the presets:
    // docs/engine/renderer.md#presets
    const PresetRestore restore;
    auto settings = coney::platform::playerSettings(GameConsoleSources{});
    GameSetting* preset = find(settings, "display_preset");
    REQUIRE(preset != nullptr);
    CHECK(preset->get() == std::string(coney::displayPresetName(coney::displaySettings().preset())));
    CHECK(preset->set("xbox").value() == "xbox");
    CHECK(coney::displaySettings().preset() == coney::DisplayPreset::Xbox);
    CHECK(preset->get() == "xbox");
    CHECK(preset->set("high").value() == "high");
    CHECK(coney::displaySettings().preset() == coney::DisplayPreset::High);
    // A name that is no preset's changes nothing (and is refused, as the choice setting does).
    CHECK_FALSE(preset->set("ultra").has_value());
    CHECK(coney::displaySettings().preset() == coney::DisplayPreset::High);
    CHECK(preset->set("ps2").value() == "ps2");
    CHECK(coney::displaySettings().preset() == coney::DisplayPreset::Ps2);
}

TEST_CASE("vsync is left out where the renderer has no window") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (vsync is left out without a window)
    // The NULL renderer has no window, so no vsync to set.
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    GameConsoleSources sources;
    sources.renderer = engine->get();
    auto settings = coney::platform::playerSettings(sources);
    CHECK(find(settings, "vsync") == nullptr);
    // No renderer at all is the same.
    auto none = coney::platform::playerSettings(GameConsoleSources{});
    CHECK(find(none, "vsync") == nullptr);
}

TEST_CASE("the vsync setting reads and sets the renderer's vsync") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (vsync is on or off)
    // The setting itself needs no window (playerSettings() offers it in one only), so the NULL renderer will do.
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    GameSetting vsync = coney::platform::vsyncSetting(**engine);
    CHECK(vsync.name == "vsync");
    CHECK(vsync.get() == "on");
    CHECK(vsync.set("off").value() == "off");
    CHECK_FALSE((*engine)->vsync());
    CHECK(vsync.get() == "off");
    CHECK(vsync.set("on").value() == "on");
    CHECK((*engine)->vsync());
    CHECK_FALSE(vsync.set("maybe").has_value());
    CHECK((*engine)->vsync());
}

TEST_CASE("the player's settings are listed in a fixed order, by what exists") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (settings lists in this order)
    auto audio = AudioOutput::start(AudioSink::Offline);
    REQUIRE(audio.has_value());
    FramePacer pacer(60);
    GameConsoleSources sources;
    sources.pacer = &pacer;
    sources.audio = audio->get();
    CHECK(namesOf(coney::platform::playerSettings(sources)) ==
          std::vector<std::string>{"fps_cap", "volume", "display_preset"});
    // The display preset is always there.
    CHECK(namesOf(coney::platform::playerSettings(GameConsoleSources{})) == std::vector<std::string>{"display_preset"});
}

TEST_CASE("the links answer the needs from the session's facts, and no profile exists yet") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog (a game and a disc need a session, a level one
    // in play, and there is no profile system to meet a profile)
    SessionFacts facts;
    GameConsoleSources sources;
    sources.session = [&facts] { return facts; };
    const GameConsoleLinks links = coney::platform::makeGameConsoleLinks(sources);
    REQUIRE(links.has);

    // No session.
    for (const Need need : {Need::Game, Need::Disc, Need::Level, Need::Profile}) {
        CHECK_FALSE(links.has(need));
    }
    // A session between levels: the game and the disc, not a level.
    facts.exists = true;
    CHECK(links.has(Need::Game));
    CHECK(links.has(Need::Disc));
    CHECK_FALSE(links.has(Need::Level));
    CHECK_FALSE(links.has(Need::Profile));
    // A level in play.
    facts.inPlay = true;
    CHECK(links.has(Need::Level));
    CHECK_FALSE(links.has(Need::Profile));

    // A console made with no session source meets none of them.
    const GameConsoleLinks bare = coney::platform::makeGameConsoleLinks(GameConsoleSources{});
    for (const Need need : {Need::Game, Need::Disc, Need::Level, Need::Profile}) {
        CHECK_FALSE(bare.has(need));
    }
}

TEST_CASE("the links name the version, the build, the disc's region and the renderer") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (version); NTSC-U is the stand-in for the
    // region (the only disc Coney knows)
    SessionFacts facts;
    GameConsoleSources sources;
    sources.session = [&facts] { return facts; };
    sources.rendererKind = RendererKind::Gpu;
    GameConsoleLinks links = coney::platform::makeGameConsoleLinks(sources);
    REQUIRE(links.buildInfo);

    // Without a session: no disc, so no region; the release build says so.
    coney::console::BuildInfo info = links.buildInfo();
    CHECK(info.version == std::string(coney::install::coneyVersion()));
    CHECK(info.build == "release build");
    CHECK(info.discRegion.empty());
    CHECK(info.renderer == "gpu");

    // With a session: the disc's region (the stand-in).
    facts.exists = true;
    CHECK(links.buildInfo().discRegion == "NTSC-U");

    // The debug tools' build says so, and a GL3 run names its renderer.
    sources.debugTools = true;
    sources.rendererKind = RendererKind::Gl3;
    links = coney::platform::makeGameConsoleLinks(sources);
    info = links.buildInfo();
    CHECK(info.build == "debug tools build");
    CHECK(info.renderer == "gl3");
}

TEST_CASE("the links name the level only while one is in play") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (where)
    SessionFacts facts;
    GameConsoleSources sources;
    sources.session = [&facts] { return facts; };
    const GameConsoleLinks links = coney::platform::makeGameConsoleLinks(sources);
    REQUIRE(links.levelName);

    CHECK(links.levelName().empty());
    // A level that is named but not in play (the front end) is none.
    facts.exists = true;
    facts.level = "level1";
    CHECK(links.levelName().empty());
    facts.inPlay = true;
    CHECK(links.levelName() == "level1");
    // The session ending takes the level with it.
    facts.exists = false;
    CHECK(links.levelName().empty());
}

TEST_CASE("the links carry the settings and the bug report writer they were given") {
    // Expected: Coney's own rule, docs/guides/console.md#in-the-game (the links hold what the sources hold)
    GameConsoleSources sources;
    sources.writeBugReport = [] { return std::string("wrote the report"); };
    const GameConsoleLinks links = coney::platform::makeGameConsoleLinks(sources);
    REQUIRE(links.writeBugReport);
    CHECK(links.writeBugReport() == "wrote the report");
    CHECK(namesOf(links.settings) == std::vector<std::string>{"display_preset"});
    // The mission select's calls stay empty and no feature is built, as there is no profile system.
    CHECK_FALSE(links.completedMissions);
    CHECK_FALSE(links.startMission);
    CHECK(links.builtFeatures.empty());
}
