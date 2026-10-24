// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that a flash is used as the original uses it (docs/research/combat-rage.md#rage,
// "The flash clip's events"; issue #234): `level80` checkpoint 2 played in the game session as `coney --play-level
// level80 --checkpoint 2` plays it (support/disc_session.h), player 1 hurt and carrying a flash, and a pad driver that
// taps d-pad right for one frame. The clip 665 `gen_flash_use` must play (it is in the player's animation set), the
// flash is spent and the health filled 19 updates into it, at the clip's action event, and on that update interface
// cue 23 (`vags/misc/flash`) and the clip's animation sound 70 sound. Standing still, walking and running are each
// tried, so a held stick does not turn the use into an instant heal. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/dealer_goal.h"
#include "audio/game_sound.h"
#include "audio/sound_engine.h"
#include "combat/meters.h"
#include "core/input_script.h"
#include "core/name_hash.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "hud/hud.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/created_humans.h"
#include "warriors/inventory.h"

namespace {

// The flash's clip: 665 SPECIAL_FLASH, `gen_flash_use`.
constexpr std::uint32_t kFlashClip = 665;
// The update of the clip its action event falls on: frame 19 of 30 per second (docs/research/combat-rage.md#rage).
constexpr int kEventUpdate = 19;
// The interface cue `Flash_Use` plays: 23, `vags/misc/flash`.
constexpr std::size_t kFlashCue = 23;
// How long anything here may take: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;
// A forward stick that walks (under the run's 0.95) and one that runs, as fractions of full deflection.
constexpr float kWalkStick = 0.6F;
constexpr float kRunStick = 1.0F;

// What the test wants of the driver now.
enum class Want : std::uint8_t {
    Nothing,   // the pad at rest (the left stick held at `stick` when it is not 0)
    SkipScene, // cross tapped every second, to skip the level's opening scene
    Flash,     // d-pad right pressed for one frame, then back to Nothing
    WalkTo,    // the left stick toward `goal` at kWalkStick until within kNear of it
};

// How near the driver walks before letting go of the stick, m: inside the dealer's 1.75 m reach.
constexpr float kNear = 1.2F;

// A pad driven by what the test asks for: the left stick up at `stick` of full deflection (through the camera, up on
// the stick is away from it, which moves the player somewhere; the heading does not matter here) and d-pad right once.
class FlashDriver final : public coney::InputSource {
  public:
    explicit FlashDriver(std::function<const coney::human::Player*()> player = {}) : m_player(std::move(player)) {}

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        ++m_frame;
        if (stick != 0.0F) {
            pad.sticks[3] = coney::stickByteFromPercent(static_cast<int>(std::lround(-stick * 100.0F)));
        }
        if (tapTriangle) {
            pad.buttons = coney::pad::kTriangle;
            tapTriangle = false;
        }
        switch (want) {
        case Want::Nothing:
            break;
        case Want::SkipScene: {
            constexpr std::uint64_t kTapEvery = 30;
            pad.buttons = m_frame % kTapEvery == 0 ? coney::pad::kCross : 0;
            break;
        }
        case Want::Flash:
            pad.buttons = coney::pad::kRight;
            want = Want::Nothing;
            break;
        case Want::WalkTo:
            if (const coney::human::Player* player = m_player ? m_player() : nullptr; player != nullptr) {
                const coney::anim::Vec3 feet = player->human().position();
                const float dx = goal.x - feet.x;
                const float dy = goal.y - feet.y;
                if (std::hypot(dx, dy) > kNear) {
                    steer(pad, *player, dx, dy);
                } else {
                    arrived = true;
                }
            }
            break;
        }
        return samples;
    }

    Want want = Want::Nothing;
    float stick = 0.0F;
    bool tapTriangle = false; // triangle pressed for one frame, once
    coney::anim::Vec3 goal{};
    bool arrived = false;

  private:
    // Sets the left stick to move toward (dx, dy) at kWalkStick, turned by the shown camera's heading: up is the
    // camera's view along the ground, right its right (docs/research/characters.md#input).
    static void steer(coney::PadSample& pad, const coney::human::Player& player, float dx, float dy) {
        const coney::human::PlayerSnapshot& shot = player.current();
        float fx = shot.cameraTarget.x - shot.cameraEye.x;
        float fy = shot.cameraTarget.y - shot.cameraEye.y;
        const float forward = std::hypot(fx, fy);
        const float wanted = std::hypot(dx, dy);
        if (forward < 1e-4F || wanted < 1e-4F) {
            return;
        }
        fx /= forward;
        fy /= forward;
        const float up = ((dx * fx) + (dy * fy)) / wanted;
        const float right = ((dx * fy) - (dy * fx)) / wanted;
        pad.sticks[2] = raw(right * kWalkStick);
        pad.sticks[3] = raw(-up * kWalkStick);
    }

    // The raw byte for `value` in [-1, 1] as an input script writes it, past each axis's dead zone (core/pad.h).
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }

    std::function<const coney::human::Player*()> m_player;
    std::uint64_t m_frame = 0;
};

// What one use of the flash did, update by update from d-pad right.
struct Use {
    bool played = false;              // 665 became player 1's anim
    int startUpdate = -1;             // the update 665 first showed (counted from the press, 0 being the press's own)
    int healUpdate = -1;              // the update the flash was spent and the health filled
    int cueUpdate = -1;               // the update interface cue 23's sound was asked for
    int animSoundUpdate = -1;         // the update the humans' animation sounds count rose
    int flashesAfter = -1;            // the flashes left at the end
    int healthAfter = -1;             // the health at the end
    int healthBefore = -1;            // the health at the press
    int healthFilledUpdate = -1;      // the first update the health was at its maximum
    int healthMaximum = -1;           // the maximum
    int cueSounds = 0;                // how many times cue 23's sound was asked for
    std::uint64_t unheard = 0;        // plays the engine could not sound: unknown, refused or without a sample
    std::vector<std::uint32_t> clips; // the player's anim at each update
};

// The humans' animation sounds asked for so far, read from the game sound's summary line ("N animation sounds and
// ...").
std::uint64_t animSoundsAsked(const coney::audio::GameSound& game) {
    const std::string summary = game.summary();
    constexpr std::string_view kPrefix = "game sound: ";
    const std::size_t at = summary.find(kPrefix);
    if (at == std::string::npos) {
        return 0;
    }
    return std::stoull(summary.substr(at + kPrefix.size()));
}

// The speech lines said so far, read from the game sound's summary line ("...; N lines said").
std::uint64_t linesSaid(const coney::audio::GameSound& game) {
    const std::string summary = game.summary();
    constexpr std::string_view kSuffix = " lines said";
    const std::size_t end = summary.find(kSuffix);
    if (end == std::string::npos) {
        return 0;
    }
    const std::size_t begin = summary.rfind(' ', end - 1) + 1;
    return std::stoull(summary.substr(begin, end - begin));
}

// The plays the engine could not sound so far: a hash with no sound, a play admission refused, a sound whose sample is
// not in the bank loaded (silent, docs/research/sound.md#play).
std::uint64_t unheardPlays(const coney::audio::SoundEngine& engine) {
    const coney::audio::SoundEngineStats stats = engine.stats();
    return stats.unknown + stats.refused + stats.missingSamples;
}

// How useFlash() finds player 1: hurt to half health and given one flash by the test, or as the game left him.
enum class SetUp : std::uint8_t { HurtWithFlash, AsPlayed };

// Plays the use: with SetUp::HurtWithFlash hurts player 1 to half health with one flash, optionally holding the stick,
// taps d-pad right and follows 90 updates (3 s), noting what happens when; with `pressAgainAt` it taps d-pad right
// again on that update (while 665 plays, which must neither restart the clip nor spend a second flash).
Use useFlash(coney::test::DiscSession& run, FlashDriver& driver, float stick, int pressAgainAt = -1,
             SetUp setUp = SetUp::HurtWithFlash) {
    // The play mode hands out player 1 const; the test hurts him as a fight would.
    auto& human = const_cast<coney::human::Human&>(run.session().play()->player().human());
    coney::Inventory& inventory = run.session().flow().state().player.inventory;
    coney::combat::Health& health = human.fighter().health();
    if (setUp == SetUp::HurtWithFlash) {
        inventory.set(0, coney::item::kRevive, 1);
        health.set(health.maximum() / 2);
    }

    coney::audio::SoundEngine* engine = run.audio().sounds().engine();
    REQUIRE(engine != nullptr);
    const std::uint32_t cueHash = engine->interfaceSound(kFlashCue);
    Use use;
    use.healthMaximum = health.maximum();
    use.healthBefore = health.value();
    int update = -1;
    engine->setPlayObserver([&](std::uint32_t hash) {
        if (hash == cueHash && cueHash != 0) {
            ++use.cueSounds;
            if (use.cueUpdate < 0) {
                use.cueUpdate = update;
            }
        }
    });

    driver.stick = stick;
    driver.want = Want::Flash;
    const std::uint64_t unheardBefore = unheardPlays(*engine);
    std::uint64_t animSounds = animSoundsAsked(run.audio().game());
    int flashes = inventory.count(0, coney::item::kRevive);
    for (update = 0; update < 90; ++update) {
        if (update == pressAgainAt) {
            driver.want = Want::Flash;
        }
        run.run(1);
        const std::uint32_t id = human.animator().animId();
        use.clips.push_back(id);
        if (id == kFlashClip && !use.played) {
            use.played = true;
            use.startUpdate = update;
        }
        const int left = inventory.count(0, coney::item::kRevive);
        if (left < flashes && use.healUpdate < 0) {
            use.healUpdate = update;
        }
        flashes = left;
        if (use.healthFilledUpdate < 0 && health.value() == health.maximum()) {
            use.healthFilledUpdate = update;
        }
        const std::uint64_t now = animSoundsAsked(run.audio().game());
        if (now > animSounds && use.animSoundUpdate < 0 && use.healUpdate >= 0) {
            use.animSoundUpdate = update;
        }
        animSounds = now;
    }
    engine->setPlayObserver({});
    driver.stick = 0.0F;
    use.unheard = unheardPlays(*engine) - unheardBefore;
    use.flashesAfter = inventory.count(0, coney::item::kRevive);
    use.healthAfter = health.value();
    return use;
}

// Checks one use against the original: 665 plays, and 19 updates into it (its action event) the flash is spent, the
// health filled (not before: it stays as it was until then), cue 23 and the clip's animation sound 70 sound on that
// update, and nothing the engine was asked to play was left silent.
void checkUse(const Use& use, const char* what) {
    std::printf("  %s: 665 %s from update %d; spent at %d, cue 23 at %d (x%d), animation sound at %d; flashes 1 -> %d, "
                "health %d -> %d of %d, full from update %d; %llu plays silent\n",
                what, use.played ? "plays" : "does not play", use.startUpdate, use.healUpdate, use.cueUpdate,
                use.cueSounds, use.animSoundUpdate, use.flashesAfter, use.healthBefore, use.healthAfter,
                use.healthMaximum, use.healthFilledUpdate, static_cast<unsigned long long>(use.unheard));
    INFO(what);
    CHECK(use.played);
    CHECK(use.flashesAfter == 0);
    CHECK(use.healthBefore < use.healthMaximum);
    CHECK(use.healthAfter == use.healthMaximum);
    // Nothing is spent or healed before the clip's event: the use lands 19 updates after 665 began.
    REQUIRE(use.startUpdate >= 0);
    CHECK(use.healUpdate == use.startUpdate + kEventUpdate);
    CHECK(use.healthFilledUpdate == use.healUpdate);
    CHECK(use.cueUpdate == use.healUpdate);
    CHECK(use.cueSounds == 1);
    CHECK(use.animSoundUpdate == use.healUpdate);
    CHECK(use.unheard == 0);
}

// Starts `level` at `checkpoint` as `coney --play-level` does and runs until player 1 has the pad (the opening scene
// skipped), then a second more.
void startPlaying(coney::test::DiscSession& run, FlashDriver& driver, const char* level, int checkpoint) {
    run.session().startAtLevel(level, checkpoint);
    REQUIRE(run.runUntilPlay());
    driver.want = Want::SkipScene;
    REQUIRE(run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit));
    driver.want = Want::Nothing;
    run.run(30);
}

} // namespace

TEST_CASE("the disc's level80: d-pad right with a flash plays 665 and uses it 19 updates in", "[disc][story][flash]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    FlashDriver driver;
    coney::test::DiscSession run(*wad, driver);
    startPlaying(run, driver, "level80", 2);

    // The clip is in the player's animation set, and the level's scripts registered the flash's interface cue.
    const coney::human::Human& human = run.session().play()->player().human();
    CHECK(human.anims().clip(kFlashClip) != nullptr);
    const coney::audio::SoundEngine* engine = run.audio().sounds().engine();
    REQUIRE(engine != nullptr);
    CHECK(engine->interfaceSound(kFlashCue) != 0);

    struct Case {
        const char* name;
        float stick;
    };
    for (const Case& test :
         {Case{"level80 standing", 0.0F}, Case{"level80 walking", kWalkStick}, Case{"level80 running", kRunStick}}) {
        checkUse(useFlash(run, driver, test.stick), test.name);
        // Let go of the stick and let the clip finish before the next case.
        driver.stick = 0.0F;
        run.run(60);
    }
    CHECK(run.session().flow().scripts().errors() == 0);
}

TEST_CASE("the disc's level99 dealer: buying a flash plays the money pair, then d-pad right plays 665 and uses it",
          "[disc][story][flash]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession* current = nullptr;
    FlashDriver driver([&current]() -> const coney::human::Player* {
        coney::platform::PlayLevelMode* play = current != nullptr ? current->session().play() : nullptr;
        return play != nullptr ? &play->player() : nullptr;
    });
    coney::test::DiscSession run(*wad, driver);
    current = &run;
    // Checkpoint 3 as the mission plays it, at the step where the dealer is set up (`P3.DealerPoizo`, which puts the
    // players at the dealer's and arms his trigger sphere, docs/research/scripting.md#level99), with the money the
    // fence's removal leaves.
    startPlaying(run, driver, "level99", 3);
    REQUIRE(run.session().flow().scripts().call("P3.DealerPoizo"));
    coney::Inventory& inventory = run.session().flow().state().player.inventory;
    inventory.set(0, coney::item::kMoney, 100);
    inventory.set(0, coney::item::kRevive, 0);
    run.run(120);

    // The dealer's brain, found by his goal.
    const coney::ai::Brains* found = run.session().gameplay().brains();
    REQUIRE(found != nullptr);
    auto& brains = const_cast<coney::ai::Brains&>(*found);
    coney::ai::Brain* dealer = nullptr;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const coney::ai::Goal* goal = brains.at(i).topGoal();
        if (goal != nullptr && goal->type() == coney::ai::GoalType::Dealer) {
            dealer = &brains.at(i);
        }
    }
    REQUIRE(dealer != nullptr);

    // The player walks up to him, within his 1.75 m, and the offer is shown.
    const coney::human::Human& human = run.session().play()->player().human();
    driver.goal = dealer->human().position();
    driver.want = Want::WalkTo;
    REQUIRE(run.runUntil([&driver] { return driver.arrived; }, kLimit));
    driver.want = Want::Nothing;
    run.run(10);
    const coney::hud::Hud& hud = run.session().flow().hud();
    CHECK_FALSE(hud.actionPrompt(0).empty());

    // Triangle: the money pair (the dealer's 668 as `money_take`, the buyer's as `money_give`), and 17 updates in the
    // flash arrives, $20 paid, with the pickup sound.
    coney::audio::SoundEngine* engine = run.audio().sounds().engine();
    REQUIRE(engine != nullptr);
    const std::uint32_t powerup = coney::crc32("vags/interface/powerup");
    int powerups = 0;
    engine->setPlayObserver([&powerups, powerup](std::uint32_t hash) { powerups += hash == powerup ? 1 : 0; });
    const int moneyBefore = inventory.count(0, coney::item::kMoney);
    const std::uint64_t linesBefore = linesSaid(run.audio().game());
    driver.tapTriangle = true;
    int buyerStart = -1;
    int dealerStart = -1;
    int flashAt = -1;
    constexpr int kDealerPair = 668;
    for (int update = 0; update < 120; ++update) {
        run.run(1);
        if (buyerStart < 0 && human.animator().animId() == kDealerPair) {
            buyerStart = update;
        }
        if (dealerStart < 0 && dealer->human().animator().animId() == kDealerPair) {
            dealerStart = update;
        }
        if (flashAt < 0 && inventory.count(0, coney::item::kRevive) > 0) {
            flashAt = update;
        }
    }
    engine->setPlayObserver({});
    constexpr int kPairEvent = 17;
    std::printf("  level99 dealer: buyer 668 from update %d, dealer 668 from %d, flash at %d; money %d -> %d; pickup "
                "sound x%d\n",
                buyerStart, dealerStart, flashAt, moneyBefore, inventory.count(0, coney::item::kMoney), powerups);
    CHECK(buyerStart >= 0);
    CHECK(dealerStart >= 0);
    CHECK(inventory.count(0, coney::item::kRevive) == 1);
    CHECK(inventory.count(0, coney::item::kMoney) == moneyBefore - 20);
    REQUIRE(buyerStart >= 0);
    CHECK(flashAt == buyerStart + kPairEvent);
    CHECK(powerups == 1);
    // His line about the deal (`cash`) is said with the buyer as its target.
    CHECK(linesSaid(run.audio().game()) > linesBefore);

    // Past the script's 3 s for the flash command (`P3.FlashSpeak`), hurt and with the flash: d-pad right. The player
    // is still facing the dealer in the fight stance (the stance idle must not put itself over 665).
    run.run(150);
    checkUse(useFlash(run, driver, 0.0F, 5), "level99 after the purchase");
    CHECK(run.session().flow().scripts().errors() == 0);
}

TEST_CASE(
    "the disc's level99 from the story: hurt at checkpoint 2, teleported to the dealer, the flash bought heals at "
    "665's event",
    "[disc][story][flash]") {
    // Issue #234, the player's path: a new story's level99, its checkpoints in one session as the story plays them,
    // the dealer reached with the stick and the flash bought with triangle, then d-pad right with the health the game
    // left him (docs/research/teleports.md#flash-lesson). The test calls the end of each checkpoint's play
    // (P1.Cleanup, P2.NavigationSection) and the fence's break (P3.FenceBroken), as the hand-over, street and fence
    // tests do: the course, the street lesson and the roofs are played by those tests, not here.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession* current = nullptr;
    FlashDriver driver([&current]() -> const coney::human::Player* {
        coney::platform::PlayLevelMode* play = current != nullptr ? current->session().play() : nullptr;
        return play != nullptr ? &play->player() : nullptr;
    });
    coney::test::DiscSession run(*wad, driver);
    current = &run;
    coney::StartUpFlow& flow = run.session().flow();
    coney::script::ScriptSystem& scripts = flow.scripts();

    // A new story, its first mission from checkpoint 1, every scene skipped with cross.
    run.session().startStory();
    REQUIRE(run.runUntil([&flow] { return flow.state().levels.find("level99").has_value(); },
                         coney::test::DiscSession::kPlayLimit));
    REQUIRE(run.session().jumpToLevel("level99", 1));
    REQUIRE(run.runUntilPlay());
    driver.want = Want::SkipScene;
    run.run(300);
    const auto health = [&run] { return run.session().play()->player().human().health(); };

    // Checkpoint 2: the new Rembrandt is handed the player and P2.SetupLesson1 sets him to 35 %; the street scene
    // l99_c2 that follows (its join and its release place him) leaves him there.
    REQUIRE(scripts.call("P1.Cleanup"));
    run.run(600);
    CHECK(flow.state().checkPoint == 2);
    const int hurt = health().value();
    const int maximum = health().maximum();
    std::printf("  level99 story: checkpoint 2, health %d of %d\n", hurt, maximum);
    CHECK(hurt == maximum * 35 / 100);

    // Checkpoint 3: the street lesson's end (walking back to Vermin) sets it and loads the roofs' chapter.
    const coney::HumanCreation* player = flow.humans().player(1);
    REQUIRE(player != nullptr);
    const std::array<coney::script::Value, 2> reached{coney::script::Value(player->handle),
                                                      coney::script::Value(player->handle)};
    REQUIRE(scripts.call("P2.NavigationSection", reached));
    REQUIRE(run.runUntil([&scripts] { return scripts.hasFunction("P3.DealerPoizo"); }, kLimit));
    run.run(900);
    CHECK(flow.state().checkPoint == 3);
    driver.want = Want::Nothing;

    // The rotting fence broken (P3.FenceBroken, which tops his money up to $20), and half a second later the cut to the
    // dealer (P3.DealerPoizo) teleports him there: still hurt.
    REQUIRE(scripts.call("P3.FenceBroken"));
    run.run(135);
    std::printf("  level99 story: at the dealer, health %d of %d\n", health().value(), maximum);
    CHECK(health().value() == hurt);

    // To the dealer with the stick at 60 %, triangle: the money pair and the flash.
    const coney::ai::Brains* found = run.session().gameplay().brains();
    REQUIRE(found != nullptr);
    const coney::ai::Brain* dealer = nullptr;
    for (std::size_t i = 0; i < found->size(); ++i) {
        const coney::ai::Goal* goal = found->at(i).topGoal();
        if (goal != nullptr && goal->type() == coney::ai::GoalType::Dealer) {
            dealer = &found->at(i);
        }
    }
    REQUIRE(dealer != nullptr);
    driver.goal = dealer->human().position();
    driver.want = Want::WalkTo;
    REQUIRE(run.runUntil([&driver] { return driver.arrived; }, kLimit));
    driver.want = Want::Nothing;
    run.run(10);
    driver.tapTriangle = true;
    // Past the script's 3 s for the flash command (P3.CheckForFlash, then P3.FlashSpeak).
    run.run(150);
    REQUIRE(flow.state().player.inventory.count(0, coney::item::kRevive) == 1);

    // d-pad right as the game left him: 665, and the heal at its event, not before.
    checkUse(useFlash(run, driver, 0.0F, -1, SetUp::AsPlayed), "level99 story");
    CHECK(scripts.errors() == 0);
}
