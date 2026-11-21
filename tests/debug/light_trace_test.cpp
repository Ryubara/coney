// SPDX-License-Identifier: GPL-3.0-or-later
// The replay of the original's light-selection trace (src/debug/light_trace.h, docs/guides/realtime-lighting.md
// #test-selection): a trace's three files read back, each call's inputs fed to Coney's LightManager::select(), and
// every call whose lights differ reported. The unit tests run on synthetic traces written here, whose expected lights
// follow the rule of LightManager_SelectLights (0x0017de10, docs/research/lighting.md#select). The `[trace]` test
// replays a real trace of the original from PCSX2 when CONEY_LIGHT_TRACE names its selections file, and prints counts
// only.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "debug/light_trace.h"
#include "support/fixtures.h"

using coney::debug::LightReplay;
using coney::debug::TraceCall;
using coney::debug::TraceList;

namespace {

// A light of a synthetic trace: RenderWare's type, radius, colour and position.
struct FixtureLight {
    std::uint32_t type = 0;
    float radius = 0.0F;
    std::array<float, 4> colour{};
    std::array<float, 3> position{};
};

// A call of a synthetic trace, the fields in the trace's columns.
struct FixtureCall {
    std::uint32_t caller = 0;
    float distSq = 0.0F;
    std::array<float, 4> sphere{};
    int flags = 0;
    int pointLights = 0;
    int pulse = 0;
    int human = 0;
    std::string glow; // empty for a call with no human
    std::uint32_t tick = 0;
    std::array<std::vector<FixtureLight>, 5> lists; // a, b, c, objects, world
    std::vector<FixtureLight> chosen;
};

constexpr std::uint32_t kHuman = 0x001746f8;
constexpr std::uint32_t kSector = 0x00411aa8;
constexpr std::uint32_t kObject = 0x001803a4;
constexpr std::uint32_t kBackground = 0x0040d1a8;

// The level's lights, Coney's own values: the world ambient (light A), two directional lights and an ambient for
// objects, the objects' pulse ambient, and lamps.
const FixtureLight kWorldAmbient{2, 0.0F, {0.2F, 0.2F, 0.2F, 1.0F}, {0, 0, 0}};
const FixtureLight kMoon{1, 0.0F, {0.25F, 0.25F, 0.375F, 1.0F}, {0, 0, 0}};
const FixtureLight kReflected{1, 0.0F, {0.125F, 0.125F, 0.125F, 1.0F}, {0, 0, 0}};
const FixtureLight kObjectAmbient{2, 0.0F, {0.25F, 0.25F, 0.25F, 1.0F}, {0, 0, 0}};
const FixtureLight kPulseAmbient{2, 0.0F, {0.0625F, 0.0625F, 0.0625F, 1.0F}, {0, 0, 0}};

// A white lamp of `radius` at (x, y, z).
FixtureLight lamp(float x, float y, float z, float radius) {
    return FixtureLight{0x80, radius, {0.75F, 0.75F, 0.5F, 1.0F}, {x, y, z}};
}

// A call of `caller` with its arguments; no pulse, no human, no lists and no lights chosen yet.
FixtureCall fixtureCall(std::uint32_t caller, float distSq, std::array<float, 4> sphere, int flags, int pointLights,
                        std::uint32_t tick) {
    FixtureCall c;
    c.caller = caller;
    c.distSq = distSq;
    c.sphere = sphere;
    c.flags = flags;
    c.pointLights = pointLights;
    c.tick = tick;
    return c;
}

// A light's nine fields as the trace writes them.
std::string fields(const FixtureLight& l) {
    return std::format("{},{},{},{},{},{},{},{},{}", l.type, l.radius, l.colour[0], l.colour[1], l.colour[2],
                       l.colour[3], l.position[0], l.position[1], l.position[2]);
}

// The selections file: a header in the lab dump's columns, then one row per call.
std::string selectionsText(std::span<const FixtureCall> calls) {
    std::string text = "caller,dist_sq,sphere_x,sphere_y,sphere_z,sphere_radius,flags,point_lights,pulse,human,count";
    for (int slot = 0; slot < 8; ++slot) {
        for (const char* field : {"type", "radius", "r", "g", "b", "a", "x", "y", "z"}) {
            text += std::format(",light{}_{}", slot, field);
        }
    }
    text += '\n';
    for (const FixtureCall& c : calls) {
        text += std::format("0x{:08x},{},{},{},{},{},{},{},{},{},{}", c.caller, c.distSq, c.sphere[0], c.sphere[1],
                            c.sphere[2], c.sphere[3], c.flags, c.pointLights, c.pulse, c.human, c.chosen.size());
        for (std::size_t slot = 0; slot < 8; ++slot) {
            text += slot < c.chosen.size() ? "," + fields(c.chosen[slot]) : std::string(",,,,,,,,,");
        }
        text += '\n';
    }
    return text;
}

// The calls file: each call's tick, caller, glow byte and list lengths.
std::string callsText(std::span<const FixtureCall> calls) {
    std::string text = "call,tick,caller,glow,a,b,c,objects,world\n";
    for (std::size_t i = 0; i < calls.size(); ++i) {
        const FixtureCall& c = calls[i];
        text += std::format("{},{},0x{:08x},{},{},{},{},{},{}\n", i, c.tick, c.caller, c.glow, c.lists[0].size(),
                            c.lists[1].size(), c.lists[2].size(), c.lists[3].size(), c.lists[4].size());
    }
    return text;
}

// The lists file: every light of every list of every call.
std::string listsText(std::span<const FixtureCall> calls) {
    static constexpr std::array<std::string_view, 5> kNames{"a", "b", "c", "objects", "world"};
    std::string text = "call,list,slot,type,radius,r,g,b,a,x,y,z\n";
    for (std::size_t i = 0; i < calls.size(); ++i) {
        for (std::size_t list = 0; list < kNames.size(); ++list) {
            for (std::size_t slot = 0; slot < calls[i].lists.at(list).size(); ++slot) {
                text += std::format("{},{},{},{}\n", i, kNames.at(list), slot, fields(calls[i].lists.at(list)[slot]));
            }
        }
    }
    return text;
}

// Five calls whose recorded lights follow the rule of docs/research/lighting.md#select, one per case.
std::vector<FixtureCall> ruleCalls() {
    std::vector<FixtureCall> calls;
    const FixtureLight nearLamp = lamp(3, 1, 0, 3);
    const FixtureLight farLamp = lamp(40, 1, 0, 3);

    // 0. A sector (world, cap 8): list A, then the world lamp whose sphere meets the atomic's (10 - 64 <= 0); the far
    //    one does not (1601 - 64 > 0).
    FixtureCall sector = fixtureCall(kSector, 0, {0, 0, 0, 5}, 2, 1, 100);
    sector.lists = {{{kWorldAmbient}, {kMoon, kReflected, kObjectAmbient}, {}, {nearLamp, farLamp}, {nearLamp}}};
    sector.chosen = {kWorldAmbient, nearLamp};
    calls.push_back(sector);

    // 1. An object at 10 m (cap 6): list B, then three lamps; the fourth scores -15.75, below the smallest kept
    //    (-15, the first lamp), and replaces it; the fifth (-9.75) replaces nothing.
    FixtureCall object = fixtureCall(kObject, 100, {0, 0, 0, 1}, 1, 1, 101);
    const FixtureLight l1 = lamp(1, 0, 0, 3);
    const FixtureLight l2 = lamp(2, 0, 0, 3);
    const FixtureLight l3 = lamp(0, 0, 1.5F, 3);
    const FixtureLight l4 = lamp(0.5F, 0, 0, 3);
    const FixtureLight l5 = lamp(2.5F, 0, 0, 3);
    object.lists = {{{kWorldAmbient},
                     {kMoon, kReflected, kObjectAmbient},
                     {kPulseAmbient, kMoon, kReflected, kObjectAmbient},
                     {l1, l2, l3, l4, l5},
                     {}}};
    object.chosen = {kMoon, kReflected, kObjectAmbient, l4, l2, l3};
    calls.push_back(object);

    // 2. An object beyond 69.3 m with the pulse (cap 6 - 2 = 4): list C fills it, so no point light is looked at.
    FixtureCall far = fixtureCall(kObject, 5000, {0, 0, 70, 1}, 1, 1, 101);
    far.pulse = 1;
    far.lists = {{{kWorldAmbient},
                  {kMoon, kReflected, kObjectAmbient},
                  {kPulseAmbient, kMoon, kReflected, kObjectAmbient},
                  {lamp(0, 0, 70, 3)},
                  {}}};
    far.chosen = {kPulseAmbient, kMoon, kReflected, kObjectAmbient};
    calls.push_back(far);

    // 3. The background: list A, no point lights.
    FixtureCall background = fixtureCall(kBackground, 0, {0, 0, 0, 100}, 2, 0, 101);
    background.lists = {{{kWorldAmbient}, {}, {}, {nearLamp}, {nearLamp}}};
    background.chosen = {kWorldAmbient};
    calls.push_back(background);

    // 4. A glowing human at 6 m (glow byte 200): list B, the glow at him in his colour (in no list), then the lamp
    //    beside him.
    FixtureCall human = fixtureCall(kHuman, 36, {10, 0.9F, 10, 1}, 1, 1, 102);
    human.human = 1;
    human.glow = "200";
    const FixtureLight glow{
        0x80, 0.4F, {200.0F / 255.0F, 100.0F / 255.0F, 50.0F / 255.0F, 200.0F / 255.0F}, {10, 1, 10}};
    const FixtureLight side = lamp(11, 0, 10, 3);
    human.lists = {{{kWorldAmbient}, {kMoon, kReflected, kObjectAmbient}, {}, {side, farLamp}, {}}};
    human.chosen = {kMoon, kReflected, kObjectAmbient, glow, side};
    calls.push_back(human);
    return calls;
}

// The calls of `fixture` read back through the trace's three files.
std::vector<TraceCall> parsed(std::span<const FixtureCall> fixture) {
    auto calls = coney::debug::parseLightTrace(selectionsText(fixture), callsText(fixture), listsText(fixture));
    INFO((calls ? std::string() : calls.error().message));
    REQUIRE(calls.has_value());
    return calls ? std::move(*calls) : std::vector<TraceCall>{};
}

} // namespace

TEST_CASE("a light trace's three files read back as calls with their inputs and chosen lights", "[debug][lighting]") {
    // Expected: lighting.md#select, confirmed (code): the inputs per call and the lists the trace reads at the entry
    const std::vector<FixtureCall> fixture = ruleCalls();
    const std::vector<TraceCall> calls = parsed(fixture);
    REQUIRE(calls.size() == 5);

    const TraceCall& object = calls[1];
    CHECK(object.caller == kObject);
    CHECK(coney::debug::traceCallerName(object.caller) == "object");
    CHECK(object.distSq == 100.0F);
    CHECK(object.sphere.radius == 1.0F);
    CHECK(object.flags == 1);
    CHECK(object.pointLights);
    CHECK_FALSE(object.pulse);
    CHECK_FALSE(object.human);
    CHECK_FALSE(object.glow.has_value());
    CHECK(object.tick == 101);
    CHECK(object.list(TraceList::B).size() == 3);
    CHECK(object.list(TraceList::C).size() == 4);
    CHECK(object.list(TraceList::Objects).size() == 5);
    CHECK(object.list(TraceList::Objects)[3].position.x == 0.5F);
    REQUIRE(object.chosen.size() == 6);
    CHECK(object.chosen[0].type == 1);
    CHECK(object.chosen[3].type == 0x80);
    CHECK(object.chosen[3].radius == 3.0F);
    CHECK(object.chosen[1].colour.b == 0.125F);

    const TraceCall& human = calls[4];
    REQUIRE(human.glow.has_value());
    CHECK(human.glow.value_or(0) == 200);
    CHECK(human.glows());
    CHECK_FALSE(object.glows());
}

TEST_CASE("a light trace whose files disagree is refused", "[debug][lighting]") {
    // Expected: lighting.md#select, confirmed (code): each call has one row per file, each list its recorded length
    const std::vector<FixtureCall> fixture = ruleCalls();
    const std::string selections = selectionsText(fixture);
    const std::string calls = callsText(fixture);
    const std::string lists = listsText(fixture);

    // A list with one light more than the calls file says.
    CHECK_FALSE(coney::debug::parseLightTrace(selections, calls, lists + "0,a,1,2,0,1,1,1,1,0,0,0\n").has_value());
    // A selections file with a call the calls file does not have.
    std::vector<FixtureCall> more = fixture;
    more.push_back(fixture[3]);
    CHECK_FALSE(coney::debug::parseLightTrace(selectionsText(more), calls, lists).has_value());
    // Not the trace's columns.
    CHECK_FALSE(coney::debug::parseLightTrace("caller,dist_sq\n", calls, lists).has_value());
    // A list the trace does not name.
    CHECK_FALSE(coney::debug::parseLightTrace(selections, calls, lists + "0,d,0,2,0,1,1,1,1,0,0,0\n").has_value());
    // A number that is not one.
    std::string bad = lists;
    bad.replace(bad.find("0,a,0,2,0,"), 10, "0,a,0,2,x,");
    CHECK_FALSE(coney::debug::parseLightTrace(selections, calls, bad).has_value());
}

TEST_CASE("a light trace is read from its selections file and the two inputs files beside it", "[debug][lighting]") {
    // Expected: unverified, #1252 (Coney's own reader: the file names trace lights writes beside its CSV)
    const std::vector<FixtureCall> fixture = ruleCalls();
    const coney::test::TempDir dir;
    const auto write = [&dir](std::string_view name, const std::string& text) {
        return dir.write(name, std::as_bytes(std::span(text.data(), text.size())));
    };
    const auto selections = write("run.csv", selectionsText(fixture));
    write("run.calls.csv", callsText(fixture));
    write("run.lists.csv", listsText(fixture));

    const auto calls = coney::debug::readLightTrace(selections);
    REQUIRE(calls.has_value());
    CHECK(calls.value_or(std::vector<TraceCall>{}).size() == 5);
    CHECK_FALSE(coney::debug::readLightTrace(dir.path() / "missing.csv").has_value());
}

TEST_CASE("replaying a trace whose lights follow the original's rule finds every call equal", "[debug][lighting]") {
    // Expected: lighting.md#select, confirmed (code): the fixture's chosen lights follow the rule's four steps
    const std::vector<FixtureCall> fixture = ruleCalls();
    const std::vector<TraceCall> calls = parsed(fixture);

    const LightReplay replay = coney::debug::replayLightTrace(calls);
    INFO(coney::debug::lightReplayReport(replay));
    CHECK(replay.calls == 5);
    CHECK(replay.equal == 5);
    CHECK(replay.glowCalls == 1);
    CHECK(replay.differences.empty());

    // Coney's own choice for the object: list B, then the lamps the deepest overlap left.
    const std::vector<coney::debug::TraceLight> coney = coney::debug::replayCall(calls[1]);
    REQUIRE(coney.size() == 6);
    CHECK(coney[3].position.x == 0.5F);
    CHECK(coney[4].position.x == 2.0F);
    CHECK(coney[5].position.z == 1.5F);
}

TEST_CASE("every call whose recorded lights differ from Coney's is reported with both lists", "[debug][lighting]") {
    // Expected: lighting.md#select, confirmed (code): a full list keeps the deepest overlap, not the first lamps
    std::vector<FixtureCall> fixture = ruleCalls();
    // Call 1 as if the full list kept the first lamps; call 3 with a light the background's call cannot choose.
    fixture[1].chosen[3] = lamp(1, 0, 0, 3);
    fixture[3].chosen.push_back(kMoon);
    const std::vector<TraceCall> calls = parsed(fixture);

    const LightReplay replay = coney::debug::replayLightTrace(calls);
    CHECK(replay.calls == 5);
    CHECK(replay.equal == 3);
    REQUIRE(replay.differences.size() == 2);
    CHECK(replay.differences[0].call == 1);
    CHECK(replay.differences[0].caller == kObject);
    CHECK(replay.differences[0].tick == 101);
    CHECK(replay.differences[0].original.size() == 6);
    CHECK(replay.differences[0].coney.size() == 6);
    CHECK(replay.differences[1].call == 3);
    CHECK(replay.differences[1].original.size() == 2);
    CHECK(replay.differences[1].coney.size() == 1);

    const std::string report = coney::debug::lightReplayReport(replay);
    INFO(report);
    CHECK(report.find("calls 5, equal 3, different 2, glow calls 1") != std::string::npos);
    CHECK(report.find("call 1 (object, tick 101): original 6 [directional, directional, ambient, point, point, "
                      "point], Coney 6 [directional, directional, ambient, point, point, point]; first difference "
                      "at slot 3") != std::string::npos);
    CHECK(report.find("call 3 (background, tick 101): original 2 [ambient, directional], Coney 1 [ambient]; first "
                      "difference at slot 1") != std::string::npos);
}

TEST_CASE("a far object whose list is over its cap and a far glowing human replay as the original chose",
          "[debug][lighting]") {
    // Expected: lighting.md#select, confirmed (code) at 0x0017de10: the list is copied whole and the glow appended
    // with no check against the cap; a count at the cap or over it skips the point lights.
    std::vector<FixtureCall> fixture;
    const FixtureLight kFill{1, 0.0F, {0.0625F, 0.125F, 0.0625F, 1.0F}, {0, 0, 0}};
    const FixtureLight beside = lamp(0, 0, 80, 3);

    // 0. An object beyond 69.3 m (cap 3) with four lights in list B: all four, not the lamp that meets it.
    FixtureCall object = fixtureCall(kObject, 5000, {0, 0, 80, 1}, 1, 1, 200);
    object.lists = {{{kWorldAmbient}, {kMoon, kReflected, kObjectAmbient, kFill}, {}, {beside}, {}}};
    object.chosen = {kMoon, kReflected, kObjectAmbient, kFill};
    fixture.push_back(object);

    // 1. A glowing human beyond 69.3 m (cap 3) with three lights in list B: the three and the glow, not the lamp.
    FixtureCall human = fixtureCall(kHuman, 5000, {0, 0.9F, 80, 1}, 1, 1, 200);
    human.human = 1;
    human.glow = "200";
    const FixtureLight glow{
        0x80, 0.4F, {200.0F / 255.0F, 100.0F / 255.0F, 50.0F / 255.0F, 200.0F / 255.0F}, {0, 1, 80}};
    human.lists = {{{kWorldAmbient}, {kMoon, kReflected, kObjectAmbient}, {}, {beside}, {}}};
    human.chosen = {kMoon, kReflected, kObjectAmbient, glow};
    fixture.push_back(human);
    const std::vector<TraceCall> calls = parsed(fixture);

    const LightReplay replay = coney::debug::replayLightTrace(calls);
    INFO(coney::debug::lightReplayReport(replay));
    CHECK(replay.calls == 2);
    CHECK(replay.equal == 2);
    CHECK(replay.glowCalls == 1);
    CHECK(replay.differences.empty());
}

TEST_CASE("a glow call is compared after the glow, which is taken from the original's chosen lights",
          "[debug][lighting]") {
    // Expected: lighting.md#glow, confirmed (code): the glow comes after the copied list, for a glow byte above 10
    std::vector<FixtureCall> fixture = ruleCalls();
    // The lamp beside the human recorded at another place: the glow still matches, the lamp after it does not.
    fixture[4].chosen[4] = lamp(12, 0, 10, 3);
    const std::vector<TraceCall> calls = parsed(fixture);

    const std::vector<coney::debug::TraceLight> coney = coney::debug::replayCall(calls[4]);
    REQUIRE(coney.size() == 5);
    CHECK(coney::debug::sameLight(coney[3], calls[4].chosen[3])); // the glow, at him, radius 0.4, his colour
    CHECK(coney[3].radius == 0.4F);
    CHECK_FALSE(coney::debug::sameLight(coney[4], calls[4].chosen[4]));

    const LightReplay replay = coney::debug::replayLightTrace(calls);
    REQUIRE(replay.differences.size() == 1);
    CHECK(replay.differences[0].call == 4);
    CHECK(coney::debug::lightReplayReport(replay).find("first difference at slot 4") != std::string::npos);
}

TEST_CASE("a trace's call is found for an atomic by its caller and the nearest sphere centre within 5 cm",
          "[debug][lighting]") {
    // Expected: unverified, #1252 (Coney's own match, the one trace diff makes: docs/guides/realtime-lighting.md
    // #test-selection)
    std::vector<FixtureCall> fixture = ruleCalls();
    // A second object 4 cm from the first: the nearer one is found.
    FixtureCall twin = fixture[1];
    twin.sphere = {0.0F, 0.04F, 0.0F, 1.0F};
    fixture.push_back(twin);
    const std::vector<TraceCall> calls = parsed(fixture);
    const auto* found = coney::debug::traceCallNear(calls, kObject, coney::world::Vec3{0.0F, 0.03F, 0.0F});
    REQUIRE(found != nullptr);
    CHECK(found == &calls[5]);
    CHECK(coney::debug::traceCallNear(calls, kObject, coney::world::Vec3{0.0F, -0.04F, 0.0F}) == &calls[1]);
    // Another caller at the same place, or the same caller 6 cm away, finds nothing.
    CHECK(coney::debug::traceCallNear(calls, kHuman, coney::world::Vec3{0.0F, 0.0F, 0.0F}) == nullptr);
    CHECK(coney::debug::traceCallNear(calls, kSector, coney::world::Vec3{0.06F, 0.0F, 0.0F}) == nullptr);
    CHECK(coney::debug::traceLightText(calls[0].chosen[1]) == "point (3, 1, 0) r 3");
    CHECK(coney::debug::traceLightText(calls[0].chosen[0]) == "ambient");
}

TEST_CASE("the original's light trace replays through Coney's selection with no call different", "[trace]") {
    // Expected: lighting.md#select, confirmed (runtime): run r-20261119-light-selection, 300 of 300 by the rule
    const char* path = SDL_getenv("CONEY_LIGHT_TRACE");
    if (path == nullptr || *path == '\0') {
        SKIP("CONEY_LIGHT_TRACE is not set");
    }
    const auto calls = coney::debug::readLightTrace(path);
    INFO((calls ? std::string() : calls.error().message));
    REQUIRE(calls.has_value());
    const LightReplay replay = coney::debug::replayLightTrace(calls.value_or(std::vector<TraceCall>{}));
    const std::string report = coney::debug::lightReplayReport(replay);
    std::printf("light trace replay: %s", report.c_str());
    INFO(report);
    CHECK(replay.calls > 0);
    CHECK(replay.differences.empty());
}
