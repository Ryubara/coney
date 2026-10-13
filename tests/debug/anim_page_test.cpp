// SPDX-License-Identifier: GPL-3.0-or-later
// The Animation player page (docs/guides/debug-menu.md#animation-player), driven through the menu model over a fake
// PlayControls: its sets, the categories with their counts, a clip's item with its length, the options a play takes,
// stop, and a set that fails to load. No game, no disc.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/anim_catalog.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"

using coney::debug::AnimClipChoice;
using coney::debug::AnimPlayOptions;
using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::MenuItem;
using coney::debug::MenuPage;
using coney::debug::Place;
using coney::debug::TunableRegistry;

namespace {

// A play mode with two anim sets (the player's and one that fails to load) that records what the page plays.
class FakePlay final : public coney::debug::PlayControls {
  public:
    [[nodiscard]] std::string sceneName() const override { return "sandbox test"; }
    [[nodiscard]] coney::anim::Vec3 playerFeet() const override { return {}; }
    [[nodiscard]] float playerHeadingDegrees() const override { return 0.0F; }
    [[nodiscard]] float playerSpeed() const override { return 0.0F; }
    [[nodiscard]] std::string playerState() const override { return "idle"; }
    void teleport(const Place& /*place*/) override {}
    [[nodiscard]] std::vector<Place> places() const override { return {}; }
    [[nodiscard]] bool playerFrozen() const override { return false; }
    void setPlayerFrozen(bool /*frozen*/) override {}
    [[nodiscard]] coney::anim::Vec3 cameraEye() const override { return {}; }
    [[nodiscard]] coney::anim::Vec3 cameraTarget() const override { return {}; }
    void resetCamera() override {}
    [[nodiscard]] bool freeCamera() const override { return false; }
    void setFreeCamera(bool /*on*/) override {}
    [[nodiscard]] bool canSpawn() const override { return false; }
    std::expected<void, coney::Error> spawn(const coney::sandbox::Primitive& /*primitive*/) override { return {}; }
    [[nodiscard]] std::size_t spawnedCount() const override { return 0; }
    std::expected<void, coney::Error> clearSpawned() override { return {}; }

    [[nodiscard]] std::vector<std::string> animSets() const override { return {"Player: warr_re", "broken"}; }
    std::expected<std::vector<AnimClipChoice>, coney::Error> animClips(std::size_t set) override {
        if (set != 0) {
            return std::unexpected(coney::Error{coney::ErrorCode::NotFound, "no such character"});
        }
        // Two tackle clips and the fight idle, one from the generic set.
        return std::vector<AnimClipChoice>{
            {.id = 3, .clip = "AI_tackle_start", .seconds = 14.0F / 30.0F, .rate = 0.75F, .generic = true},
            {.id = 4, .clip = "gen_tackle_start", .seconds = 0.5F, .rate = 1.0F, .generic = false},
            {.id = 358, .clip = "fight_idle", .seconds = 1.0F, .rate = 1.0F, .generic = false},
        };
    }
    std::expected<void, coney::Error> playAnim(std::size_t set, std::uint32_t id,
                                               const AnimPlayOptions& options) override {
        playedSet = set;
        playedId = id;
        played = options;
        return {};
    }
    void stopAnim() override { ++stops; }
    [[nodiscard]] coney::debug::AnimPlayback animPlayback() const override { return playback; }

    std::optional<std::size_t> playedSet;
    std::uint32_t playedId = 0;
    AnimPlayOptions played;
    int stops = 0;
    coney::debug::AnimPlayback playback;
};

// The item labelled `label` on `page`, which must exist.
const MenuItem& itemOn(const MenuPage& page, std::string_view label) {
    const MenuItem* item = page.find(label);
    INFO(label);
    REQUIRE(item != nullptr);
    return *item;
}

// The page the submenu `label` on `page` opens.
std::shared_ptr<MenuPage> open(const MenuPage& page, std::string_view label) {
    const MenuItem& item = itemOn(page, label);
    REQUIRE(item.kind == coney::debug::ItemKind::Submenu);
    auto opened = item.open();
    REQUIRE(opened != nullptr);
    return opened;
}

} // namespace

TEST_CASE("the Animation player lists a set's clips by category with their lengths and plays one", "[debug][anim]") {
    TunableRegistry tunables;
    FakePlay play;
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    DebugSession session(tunables, services, nullptr);
    const auto page = session.model().openPage("Animation player");
    REQUIRE(page != nullptr);
    CHECK(itemOn(*page, "Playing").watch() == "nothing");

    // The player's set: its count, then the categories in order, each tagged with its clips.
    const auto set = open(*page, "Player: warr_re");
    CHECK(itemOn(*set, "Clips").watch() == "3, 2 s");
    REQUIRE(set->items().size() == 3);
    CHECK(set->items()[1].label == "Fight");
    CHECK(set->items()[1].detail == "1");
    CHECK(set->items()[2].label == "Tackle");
    CHECK(set->items()[2].detail == "2");
    const auto tackles = open(*set, "Tackle");
    REQUIRE(tackles->items().size() == 2);
    const MenuItem& intro = itemOn(*tackles, "3 AI_tackle_start 0.47 s");
    CHECK(intro.help == "ANIM_TACKLE_INTRO: 0.47 s, 0.62 s at its rate 0.75; from the generic set.");

    // Once at its own rate by default; then looping at twice the speed, in place.
    intro.run();
    REQUIRE(play.playedSet == std::optional<std::size_t>{0});
    CHECK(play.playedId == 3);
    CHECK_FALSE(play.played.loop);
    CHECK(play.played.speed == 1.0F);
    CHECK_FALSE(play.played.inPlace);
    itemOn(*page, "Loop").setBool(true);
    itemOn(*page, "Speed").setNumber(2.0);
    itemOn(*page, "In place").setBool(true);
    itemOn(*tackles, "4 gen_tackle_start 0.50 s").run();
    CHECK(play.playedId == 4);
    CHECK(play.played.loop);
    CHECK(play.played.speed == 2.0F);
    CHECK(play.played.inPlace);
    // The options stay for the next opening of the page.
    const auto again = session.model().openPage("Animation player");
    CHECK(itemOn(*again, "Speed").getNumber() == 2.0);
    CHECK(itemOn(*again, "Loop").getBool());

    // What plays shows on the page; Stop asks the mode to stop it.
    play.playback = coney::debug::AnimPlayback{
        .playing = true, .id = 4, .clip = "gen_tackle_start", .set = "", .time = 0.25F, .duration = 0.5F, .loop = true};
    CHECK(itemOn(*page, "Playing").watch() == "4 gen_tackle_start 0.25 / 0.50 s, loop");
    itemOn(*page, "Stop").run();
    CHECK(play.stops == 1);
}

TEST_CASE("an anim set that fails to load says why", "[debug][anim]") {
    TunableRegistry tunables;
    FakePlay play;
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    DebugSession session(tunables, services, nullptr);
    const auto page = session.model().openPage("Animation player");
    REQUIRE(page != nullptr);
    const auto broken = open(*page, "broken");
    REQUIRE(broken->items().size() == 1);
    CHECK(itemOn(*broken, "Not loaded").watch() == "no such character");
}
