// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/crew_status.h"

#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hud/hud.h"

// The crew status list against synthetic humans (docs/research/hud-functions-parts.md#crew-status): the states, the
// icons and the caption of CrewStatus_Add (0x001a0420) and CrewStatusEntry_Update (0x001a0d28), the removal of
// CrewStatus_Update (0x0019fda0) and CrewStatus_Remove (0x001a07f0), the portraits of CrewStatus_GetIcon (0x0019f988)
// and the default video mode's layout (0x0050cfe4-0x0050d01c, HUD_LevelSetUp's CrewStatus_Setup arguments).

using Catch::Approx;
using coney::hud::CrewFrame;
using coney::hud::CrewMember;
using coney::hud::CrewState;
using coney::hud::CrewStatusList;
using coney::hud::CrewTexts;

namespace {

// Player 1's gang in these tests, and another.
constexpr int kCrewGang = 1;
constexpr int kOtherGang = 2;
// Brain types: a Warrior and a gang soldier (ai::BrainType).
constexpr int kWarriorBrain = 3;
constexpr int kGangBrain = 2;
// Cleon's character type (portrait kind 8, rectangle 0x27).
constexpr int kCleon = 1;

// A Warrior of player 1's gang with handle `handle`, in no trouble.
CrewMember warrior(double handle, std::string name = "Cleon") {
    CrewMember member;
    member.handle = handle;
    member.gang = kCrewGang;
    member.brainType = kWarriorBrain;
    member.characterType = kCleon;
    member.name = std::move(name);
    return member;
}

// Texts that name what was asked for: `GSTRING.HUD[id]` is `S<id>`, colour slot n's markup `<Cn>`.
CrewTexts namingTexts() {
    CrewTexts texts;
    texts.hudString = [](std::uint32_t id) { return std::format("S{}", id); };
    texts.hudColour = [](int slot) { return std::format("<C{}>", slot); };
    return texts;
}

// A frame over `humans` with player slot 0 in kCrewGang.
CrewFrame frameOf(const std::vector<CrewMember>& humans) {
    CrewFrame frame;
    frame.humans = humans;
    frame.playerGangs = {kCrewGang, -1};
    return frame;
}

// One HUD update of the list as the original's frame runs it: HUD_TagAddHuman for each human (Humans_Update), then
// CrewStatus_Update.
void step(CrewStatusList& list, const std::vector<CrewMember>& humans, std::uint64_t nowMs) {
    const CrewFrame frame = frameOf(humans);
    for (const CrewMember& member : humans) {
        list.add(member, frame, namingTexts(), nowMs);
    }
    list.update(frame);
}

} // namespace

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("a knocked-out, cuffed or far-fighting crew member gets one entry with its icon and caption", "[hud][crew]") {
    // Each state: its status rectangle and its GSTRING.HUD caption (225-227), after the 2.5 s format, colour tag 5
    // and the name with a space.
    struct Case {
        const char* what;
        CrewMember member;
        std::uint32_t rect;
        std::uint32_t string;
    };
    CrewMember down = warrior(10.0);
    down.knockedOut = true;
    CrewMember cuffed = warrior(10.0);
    cuffed.cuffed = true;
    CrewMember fighting = warrior(10.0);
    fighting.hunted = true;
    fighting.attacked = true;
    fighting.cameraDistance = 50.5F;
    for (const Case& c : {Case{"knocked out", down, 0x1a, 225}, Case{"cuffed", cuffed, 0x19, 226},
                          Case{"fighting far off", fighting, 0x15, 227}}) {
        INFO(c.what);
        CrewStatusList list;
        step(list, {c.member}, 1000);
        REQUIRE(list.count() == 1);
        const CrewStatusList::Entry* entry = list.find(10.0);
        REQUIRE(entry != nullptr);
        CHECK(entry->order == 0U);
        CHECK(entry->shown);
        CHECK(entry->status == c.rect);
        CHECK(entry->portrait == 0x27U);
        CHECK(list.caption() == std::format("<DISPLAYTIME 2500><SIZE 1.0><CENTER><C5>Cleon S{}", c.string));
        CHECK(list.captionSinceMs() == 1000U);
        // Listed already: a second update adds nothing and keeps the caption's time.
        step(list, {c.member}, 1100);
        CHECK(list.count() == 1);
        CHECK(list.captionSinceMs() == 1000U);
    }
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("the fight alert needs an enemy, an attacker and more than 50 m to the camera", "[hud][crew]") {
    CrewMember member = warrior(10.0);
    member.hunted = true;
    member.attacked = true;
    member.cameraDistance = 50.0F;
    CHECK(coney::hud::crewState(member) == CrewState::None);
    member.cameraDistance = 50.01F;
    CHECK(coney::hud::crewState(member) == CrewState::FightingFar);
    member.hunted = false;
    CHECK(coney::hud::crewState(member) == CrewState::None);
    member.hunted = true;
    member.attacked = false;
    CHECK(coney::hud::crewState(member) == CrewState::None);
    // The states are tested knocked out, cuffed, fighting: the first that holds wins.
    member.attacked = true;
    member.cuffed = true;
    CHECK(coney::hud::crewState(member) == CrewState::Cuffed);
    member.knockedOut = true;
    CHECK(coney::hud::crewState(member) == CrewState::KnockedOut);
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("a crew member whose state clears is hidden at once and gone at the next update", "[hud][crew]") {
    CrewStatusList list;
    CrewMember member = warrior(10.0);
    member.cuffed = true;
    step(list, {member}, 0);
    REQUIRE(list.count() == 1);
    // A cuffed member knocked out changes icon while listed.
    member.knockedOut = true;
    step(list, {member}, 33);
    REQUIRE(list.find(10.0) != nullptr);
    CHECK(list.find(10.0)->status == 0x1aU);
    // Up and uncuffed: hidden by this update, removed by the next.
    member.knockedOut = false;
    member.cuffed = false;
    step(list, {member}, 66);
    REQUIRE(list.find(10.0) != nullptr);
    CHECK_FALSE(list.find(10.0)->shown);
    step(list, {member}, 100);
    CHECK(list.count() == 0);
    CHECK(list.find(10.0) == nullptr);
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("a listed human who is gone leaves the list and the others move up", "[hud][crew]") {
    CrewStatusList list;
    std::vector<CrewMember> humans;
    for (int i = 0; i < 3; ++i) {
        CrewMember member = warrior(10.0 + i);
        member.cuffed = true;
        humans.push_back(member);
    }
    step(list, humans, 0);
    REQUIRE(list.count() == 3);
    // The second one's handle no longer resolves: removed, and the third takes order 1.
    humans.erase(humans.begin() + 1);
    step(list, humans, 33);
    CHECK(list.count() == 2);
    CHECK(list.find(11.0) == nullptr);
    REQUIRE(list.find(10.0) != nullptr);
    REQUIRE(list.find(12.0) != nullptr);
    CHECK(list.find(10.0)->order == 0U);
    CHECK(list.find(12.0)->order == 1U);
    // Removing by handle (Human_Destroy, Human_MakePlayer) does the same.
    CHECK(list.remove(10.0));
    CHECK_FALSE(list.remove(10.0));
    REQUIRE(list.find(12.0) != nullptr);
    CHECK(list.find(12.0)->order == 0U);
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("nine crew members in trouble give eight entries", "[hud][crew]") {
    CrewStatusList list;
    std::vector<CrewMember> humans;
    for (int i = 0; i < 9; ++i) {
        CrewMember member = warrior(100.0 + i);
        member.knockedOut = true;
        humans.push_back(member);
    }
    step(list, humans, 0);
    CHECK(list.count() == 8);
    CHECK(list.find(108.0) == nullptr);
    CHECK_FALSE(list.add(humans.back(), frameOf(humans), namingTexts(), 33));
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("a bum or a member of another gang is never listed", "[hud][crew]") {
    // The bums, character types 270-287, never; the types either side are.
    for (const int type : {270, 287}) {
        CrewStatusList list;
        CrewMember bum = warrior(10.0, "Bum");
        bum.characterType = type;
        bum.cuffed = true;
        step(list, {bum}, 0);
        CHECK(list.count() == 0);
    }
    for (const int type : {269, 288}) {
        CrewStatusList list;
        CrewMember member = warrior(10.0, "Other");
        member.characterType = type;
        member.cuffed = true;
        step(list, {member}, 0);
        CHECK(list.count() == 1);
    }
    // Another gang's soldier is refused; another gang's Warrior is taken but hidden by the update, then removed.
    {
        CrewStatusList list;
        CrewMember soldier = warrior(10.0, "Soldier");
        soldier.gang = kOtherGang;
        soldier.brainType = kGangBrain;
        soldier.knockedOut = true;
        CHECK_FALSE(list.add(soldier, frameOf({soldier}), namingTexts(), 0));
        CHECK(list.count() == 0);
    }
    {
        CrewStatusList list;
        CrewMember ally = warrior(10.0);
        ally.gang = kOtherGang;
        ally.knockedOut = true;
        const std::vector<CrewMember> humans{ally};
        CHECK(list.add(ally, frameOf(humans), namingTexts(), 0));
        list.update(frameOf(humans));
        REQUIRE(list.find(10.0) != nullptr);
        CHECK_FALSE(list.find(10.0)->shown);
        list.update(frameOf(humans));
        CHECK(list.count() == 0);
    }
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("the HUD lists player 1's gang members only, and steps the list below level 100", "[hud][crew]") {
    coney::hud::Hud hud;
    CrewMember mine = warrior(10.0);
    mine.cuffed = true;
    CrewMember theirs = warrior(11.0);
    theirs.gang = kOtherGang;
    theirs.cuffed = true;
    const std::vector<CrewMember> humans{mine, theirs};
    coney::hud::HudFrame frame;
    frame.levelNumber = 80;
    frame.crew = frameOf(humans);
    frame.nowMs = 0;
    hud.update(frame);
    CHECK(hud.crewStatus().count() == 1);
    REQUIRE(hud.crewStatus().find(10.0) != nullptr);
    CHECK(hud.crewStatus().find(10.0)->shown);
    // In a level of 100 or more members are still added, but nothing updates the list: the cleared state stays.
    coney::hud::Hud rumble;
    frame.levelNumber = 100;
    rumble.update(frame);
    REQUIRE(rumble.crewStatus().count() == 1);
    mine.cuffed = false;
    const std::vector<CrewMember> cleared{mine};
    frame.crew = frameOf(cleared);
    frame.nowMs = 33;
    rumble.update(frame);
    rumble.update(frame);
    CHECK(rumble.crewStatus().count() == 1);
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("the caption capitalises the name and spells out WarriorsSoldier", "[hud][crew]") {
    CHECK(coney::hud::crewCaptionName("vermin") == "Vermin");
    CHECK(coney::hud::crewCaptionName("Snow") == "Snow");
    CHECK(coney::hud::crewCaptionName("WarriorsSoldier") == "Warriors Soldier");
    CHECK(coney::hud::crewCaptionName("") == "");
    CrewMember member = warrior(10.0, "WarriorsSoldier");
    CHECK(coney::hud::crewCaption(member, CrewState::Cuffed, namingTexts()) ==
          "<DISPLAYTIME 2500><SIZE 1.0><CENTER><C5>Warriors Soldier S226");
    CHECK(coney::hud::crewCaption(member, CrewState::None, namingTexts()).empty());
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("the status icon pulses on an 800 ms triangle wave, cubed", "[hud][crew]") {
    // The wave is 1 at 0 and 800 ms and 0 at 400 ms; the alpha share is 1 less its cube.
    CHECK(coney::hud::crewStatusAlpha(0) == Approx(0.0F));
    CHECK(coney::hud::crewStatusAlpha(100) == Approx(1.0F - (0.75F * 0.75F * 0.75F)));
    CHECK(coney::hud::crewStatusAlpha(200) == Approx(0.875F));
    CHECK(coney::hud::crewStatusAlpha(400) == Approx(1.0F));
    CHECK(coney::hud::crewStatusAlpha(600) == Approx(0.875F));
    CHECK(coney::hud::crewStatusAlpha(800) == Approx(0.0F));
    CHECK(coney::hud::crewStatusAlpha(1000) == Approx(0.875F));
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code); the kinds: characters.md#code-core
TEST_CASE("crew portraits show the Warrior outside co-op and whose gang in co-op", "[hud][crew]") {
    CrewMember member = warrior(10.0);
    CrewFrame frame;
    frame.playerGangs = {kCrewGang, kOtherGang};
    // Kinds by type: Cleon (1) 8 → 0x27, Swan (5) 0 → 0x31, type 45 kind 11 → 0x2a.
    CHECK(coney::hud::crewPortraitKind(1) == 8);
    CHECK(coney::hud::crewPortraitKind(189) == 8);
    CHECK(coney::hud::crewPortraitKind(5) == 0);
    CHECK(coney::hud::crewPortraitKind(48) == 11);
    CHECK_FALSE(coney::hud::crewPortraitKind(100).has_value());
    CHECK(coney::hud::crewPortraitRect(member, frame) == 0x27U);
    member.characterType = 5;
    CHECK(coney::hud::crewPortraitRect(member, frame) == 0x31U);
    member.characterType = 45;
    CHECK(coney::hud::crewPortraitRect(member, frame) == 0x2aU);
    // A type with no kind, or any type in co-op: 0x2c in slot 0's gang, 0x2d in slot 1's, 0x28 in neither.
    member.characterType = 100;
    CHECK(coney::hud::crewPortraitRect(member, frame) == 0x2cU);
    frame.coop = true;
    member.characterType = kCleon;
    CHECK(coney::hud::crewPortraitRect(member, frame) == 0x2cU);
    member.gang = kOtherGang;
    CHECK(coney::hud::crewPortraitRect(member, frame) == 0x2dU);
    member.gang = 7;
    CHECK(coney::hud::crewPortraitRect(member, frame) == 0x28U);
}

// Expected: hud-functions-parts.md#crew-status, confirmed (code)
TEST_CASE("crew entries stand four a row from (0.407, 0.07), shifted while fewer than four", "[hud][crew]") {
    // x 0.407 + column × 0.072, y 0.07 + row × 0.096; with fewer than four listed, x + 0.11 − 0.025 per entry after
    // the first.
    CHECK(CrewStatusList::entryPlace(0, 1).x == Approx(0.517F));
    CHECK(CrewStatusList::entryPlace(0, 1).y == Approx(0.07F));
    CHECK(CrewStatusList::entryPlace(1, 2).x == Approx(0.564F));
    CHECK(CrewStatusList::entryPlace(3, 4).x == Approx(0.623F));
    CHECK(CrewStatusList::entryPlace(4, 5).x == Approx(0.407F));
    CHECK(CrewStatusList::entryPlace(4, 5).y == Approx(0.166F));
    CHECK(CrewStatusList::entryPlace(5, 8).x == Approx(0.479F));
    // The caption: x 0.487 + 0.01 × (count mod 4 − 1, 4 and 8 counting 1), y under the last row less 0.01.
    CHECK(CrewStatusList::captionPlaceFor(1, 1).x == Approx(0.487F));
    CHECK(CrewStatusList::captionPlaceFor(1, 1).y == Approx(0.156F));
    CHECK(CrewStatusList::captionPlaceFor(3, 3).x == Approx(0.507F));
    CHECK(CrewStatusList::captionPlaceFor(4, 4).x == Approx(0.487F));
    CHECK(CrewStatusList::captionPlaceFor(5, 5).y == Approx(0.252F));
    // An empty list's caption goes far off the screen: the original's counts are unsigned, so "less one" wraps.
    CHECK(CrewStatusList::captionPlaceFor(0, 0).x > 1.0e6F);
    CHECK(CrewStatusList::captionPlaceFor(0, 0).y > 1.0e6F);
}
