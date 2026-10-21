// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: Rembrandt (loaded from the disc) grabs the fight yard's front target
// (assets/sandbox/combat.layout, a passive human::TargetHuman with Rembrandt's clips) and throws it with the stick at
// 0.9 at several angles from his facing, in open ground. With the disc's own throw clips, the body flies along the
// stick as the original's does at runtime (docs/research/combat-throws.md#throw-direction). Driven by in-memory input
// scripts on the fixed 30 Hz step with no window and no clock. They run only when the environment variable CONEY_DISC
// names the disc and skip otherwise; they print angles and distances only, never data (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "characters/character_data.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/pair_placement.h"
#include "human/player.h"
#include "human/target_human.h"
#include "sandbox/sandbox_world.h"
#include "support/test_paths.h"

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegrees = kPi / 180.0F;

// The update circle is pressed on: well after the grab (circle tapped at 10) has attached its victim.
constexpr std::uint64_t kThrowFrame = 60;
// Updates run after the press: the victim has landed (196) by then.
constexpr std::uint64_t kAfterThrow = 90;

// The player's character from the disc, and the fight yard.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct Yard {
    std::optional<coney::io::Wad> wad;
    std::unique_ptr<coney::human::PlayerCharacter> character;
    std::unique_ptr<coney::sandbox::SandboxWorld> world;
};

// Loads Rembrandt from the disc at `discPath` and builds the combat layout from the repository's assets.
Yard loadYard(const char* discPath) {
    Yard yard;
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    const coney::io::Wad& discWad = yard.wad.emplace(std::move(*wad));
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    auto character = coney::human::PlayerCharacter::load(discWad, table, coney::human::kPlayerModel);
    REQUIRE(character.has_value());
    yard.character = std::move(*character);
    auto world = coney::sandbox::SandboxWorld::load(coney::test::assetsDir() / "sandbox", "combat");
    REQUIRE(world.has_value());
    REQUIRE(world->collision() != nullptr);
    yard.world = std::make_unique<coney::sandbox::SandboxWorld>(std::move(*world));
    return yard;
}

// The disc, or null when CONEY_DISC is not set (the test then skips).
const char* discPath() {
    const char* path = SDL_getenv("CONEY_DISC");
    return path != nullptr && *path != '\0' ? path : nullptr;
}

// One throw as measured, in the stick's frame (metres right of and along the stick heading, from where the thrower
// stood on the update before the press), as the research measured the original's.
struct Throw {
    float stickHeading = 0.0F;
    float afterTurn = 0.0F; // the thrower's heading 5 updates after the press, less the stick heading
    float atEnd = 0.0F;     // the same once the victim has landed
    coney::anim::Vec3 landing;
    std::uint64_t landedAfter = 0; // updates from the press to the victim's 196
    std::uint32_t throwClip = 0;
};

// Grabs the front target and throws it with the stick at 0.9, `degrees` clockwise of the facing.
Throw throwAt(const Yard& yard, float degrees) {
    const auto x = static_cast<int>(std::lround(90.0F * std::sin(degrees * kDegrees)));
    const auto y = static_cast<int>(std::lround(90.0F * std::cos(degrees * kDegrees)));
    const std::string script =
        std::format("10 tap circle\n{0} stick left {1} {2}\n{0} tap circle\n{3} stick left 0 0\n", kThrowFrame, x, y,
                    kThrowFrame + 9);
    auto events = coney::parseInputScript(script);
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    coney::Pads pads;
    const coney::raycast::CollisionMesh* mesh = yard.world->collision();
    const coney::sandbox::SandboxLayout& layout = yard.world->layout();
    REQUIRE(!layout.targets.empty());
    const coney::sandbox::TargetPoint& point = layout.targets.front();
    coney::human::Player player(*yard.character, mesh,
                                coney::human::PlayerStart{.position = layout.spawns.front().position,
                                                          .headingDegrees = layout.spawns.front().headingDegrees});
    coney::human::TargetHuman target(yard.character->anims(), coney::human::AnimSlots::player(),
                                     coney::anim::referenceRotations(), point.health, point.position,
                                     point.headingDegrees * kPi / 180.0F);
    const std::array<coney::human::Combatant*, 1> targets{&target};
    Throw out;
    coney::anim::Vec3 start;
    for (std::uint64_t frame = 0; frame <= kThrowFrame + kAfterThrow; ++frame) {
        pads.update(input.sample(frame));
        player.update(pads.port(0), mesh, targets);
        target.step();
        const coney::human::Human& human = player.human();
        if (frame == kThrowFrame - 1) {
            start = human.position();
        }
        if (frame == kThrowFrame) {
            out.stickHeading = coney::human::wrapAngle(human.intent().angle - (kPi / 2.0F));
            out.throwClip = human.animator().animId();
        }
        if (frame == kThrowFrame + 5) {
            out.afterTurn = coney::human::wrapAngle(human.heading() - out.stickHeading);
        }
        if (frame > kThrowFrame && out.landedAfter == 0 && target.animator().animId() == 196) {
            out.landedAfter = frame - kThrowFrame;
            out.landing = coney::human::toFrame(start, out.stickHeading, target.position());
        }
    }
    out.atEnd = coney::human::wrapAngle(player.human().heading() - out.stickHeading);
    std::printf("throw at %+.0f degrees: clip %u, thrower %+.1f after 5 updates and %+.1f at the end from the stick; "
                "landed (%+.2f, %.2f) m in the stick's frame, %+.1f degrees, %llu updates after\n",
                static_cast<double>(degrees), out.throwClip, static_cast<double>(out.afterTurn / kDegrees),
                static_cast<double>(out.atEnd / kDegrees), static_cast<double>(out.landing.x),
                static_cast<double>(out.landing.y),
                static_cast<double>(std::atan2(out.landing.x, out.landing.y) / kDegrees),
                static_cast<unsigned long long>(out.landedAfter));
    return out;
}

// Whether `radians` is within `degrees` of `wantedDegrees`.
bool near(float radians, float wantedDegrees, float degrees) {
    return std::fabs(coney::human::wrapAngle(radians - (wantedDegrees * kDegrees))) <= degrees * kDegrees;
}

} // namespace

TEST_CASE("on the disc, a thrown human flies along the stick wherever it points within the throw's side",
          "[sandbox][combat][disc][throw]") {
    const char* path = discPath();
    if (path == nullptr) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Yard yard = loadYard(path);
    // The stick clockwise of the facing (the page's d is anticlockwise: these are its 0, -20, 20, -40, 40, -50, -90,
    // 160 and 110) and the side's clip.
    struct Case {
        float degrees;
        std::uint32_t clip;
    };
    const std::array<Case, 9> cases{{{0.0F, 147},
                                     {20.0F, 147},
                                     {-20.0F, 147},
                                     {40.0F, 147},
                                     {-40.0F, 147},
                                     {50.0F, 149},
                                     {90.0F, 149},
                                     {-160.0F, 151},
                                     {-110.0F, 153}}};
    struct Landed {
        std::uint32_t clip;
        coney::anim::Vec3 landing;
        float atEnd;
    };
    std::vector<Landed> landed;
    for (const Case& c : cases) {
        CAPTURE(c.degrees);
        const Throw thrown = throwAt(yard, c.degrees);
        CHECK(thrown.throwClip == c.clip);
        REQUIRE(thrown.landedAfter != 0);
        // The front clip does not turn the thrower: the 1/6 s turn alone puts him on the stick, in 5 updates, and he
        // ends facing it.
        if (c.clip == 147) {
            CHECK(near(thrown.afterTurn, 0.0F, 0.5F));
            CHECK(near(thrown.atEnd, 0.0F, 0.5F));
        }
        landed.push_back(Landed{.clip = c.clip, .landing = thrown.landing, .atEnd = thrown.atEnd});
    }
    // In the stick's frame one side's throws are one throw, whatever the stick's angle within the side: the same
    // landing point (within 0.1 m; 4 cm at runtime) and the same heading at the end.
    for (const Landed& throwOf : landed) {
        const auto first = std::ranges::find(landed, throwOf.clip, &Landed::clip);
        CHECK(std::hypot(throwOf.landing.x - first->landing.x, throwOf.landing.y - first->landing.y) <= 0.1F);
        CHECK(near(throwOf.atEnd - first->atEnd, 0.0F, 0.5F));
    }
}

TEST_CASE("on the disc, the throw clips' own turns end the thrower facing the stick, as at runtime",
          "[sandbox][combat][disc][throw][!mayfail]") {
    // **Open question** (docs/research/combat.md#open-questions): Coney's side clips turn the thrower about a
    // third too far (149 by -120 degrees where the original's turned -90) and the front victim drifts 19 degrees right
    // of the stick (2.1 at runtime). Scaling the root turn by the task's rate, as the velocity is, matched the
    // runtime turns in a local experiment, but formats/animation.md#root-motion has only the velocity scaled. Until
    // the research settles it this case may fail; it prints what Coney does.
    const char* path = discPath();
    if (path == nullptr) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Yard yard = loadYard(path);
    // The thrower's heading at the end less the stick's, at runtime (front 0.0, right -0.2, rear +1.2, left -3.5).
    struct Case {
        float degrees;
        float endDegrees;
        float within;
    };
    const std::array<Case, 4> cases{
        {{0.0F, 0.0F, 0.5F}, {90.0F, -0.2F, 0.5F}, {-160.0F, 1.2F, 1.0F}, {-110.0F, -3.5F, 1.0F}}};
    for (const Case& c : cases) {
        CAPTURE(c.degrees);
        const Throw thrown = throwAt(yard, c.degrees);
        CHECK(near(thrown.atEnd, c.endDegrees, c.within));
        // The front throw lands 2.1 degrees right of the stick seen from where the thrower stood (its clip's drift).
        if (&c == &cases.front()) {
            CHECK(std::fabs((std::atan2(thrown.landing.x, thrown.landing.y) / kDegrees) - 2.1F) <= 1.0F);
        }
    }
}
