// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: Rembrandt (loaded from the disc) fighting the fight yard's front target
// (assets/sandbox/combat.layout, a passive human::TargetHuman with Rembrandt's clips): a combo, a grab with a strike,
// the spins and a throw, a tackle with a strike, and a mugging. Driven by the scripted pads in tests/support (partial
// stick deflections, square, cross, circle, triangle, R1), on the fixed 30 Hz step with no window and no clock. The
// yard is Coney's own; only the character comes from the disc. They run only when the environment variable CONEY_DISC
// names the disc and skip otherwise; they print counts, clip ids and a hash of the run only, never data (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_data.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "human/player.h"
#include "human/target_human.h"
#include "sandbox/sandbox_world.h"

using coney::human::TargetState;

namespace {

// The player's character from the disc CONEY_DISC names, and the fight yard.
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
    auto world = coney::sandbox::SandboxWorld::load(std::filesystem::path(CONEY_ASSETS_DIR) / "sandbox", "combat");
    REQUIRE(world.has_value());
    REQUIRE(world->collision() != nullptr);
    yard.world = std::make_unique<coney::sandbox::SandboxWorld>(std::move(*world));
    return yard;
}

// FNV-1a over `bytes`, continuing from `hash`.
std::uint64_t fnv1a(std::uint64_t hash, const void* bytes, std::size_t size) {
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * 0x100000001b3ULL;
    }
    return hash;
}

// What a scripted fight left: the clips each side played, in order, and the target at the end.
struct Fight {
    std::vector<std::uint32_t> playerClips;
    std::vector<std::uint32_t> targetClips;
    std::vector<TargetState> targetStates;
    std::vector<coney::combat::CombatMode> modes;
    int damage = 0;
    int hits = 0;
    int reactions = 0;
    int stuns = 0;
    int knockdowns = 0;
    int power = 0;
    std::uint64_t hash = 0;
};

// Appends `value` to `list` when it differs from the last one.
template <typename T> void addChange(std::vector<T>& list, T value) {
    if (list.empty() || list.back() != value) {
        list.push_back(value);
    }
}

// Whether `clips` holds `wanted` in that order (others may come between).
bool inOrder(const std::vector<std::uint32_t>& clips, const std::vector<std::uint32_t>& wanted) {
    std::size_t next = 0;
    for (const std::uint32_t clip : clips) {
        if (next < wanted.size() && clip == wanted[next]) {
            ++next;
        }
    }
    return next == wanted.size();
}

// Runs `frames` frames of the input script `name` (in tests/support): a fresh player at the yard's first spawn and the
// front target, stepped after him; prints the clips, counts and a hash.
Fight runFight(const Yard& yard, const char* label, const std::string& name, std::uint64_t frames) {
    const std::filesystem::path path = std::filesystem::path(CONEY_TEST_SUPPORT_DIR) / name;
    auto events = coney::loadInputScript(path.string());
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
                                     point.headingDegrees * std::numbers::pi_v<float> / 180.0F);
    const std::array<coney::human::Combatant*, 1> targets{&target};
    Fight fight;
    fight.hash = 0xcbf29ce484222325ULL;
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        pads.update(input.sample(frame));
        player.update(pads.port(0), mesh, targets);
        target.step();
        const std::uint32_t playerClip = player.human().animator().animId();
        const std::uint32_t targetClip = target.animator().animId();
        addChange(fight.playerClips, playerClip);
        addChange(fight.targetClips, targetClip);
        addChange(fight.targetStates, target.state());
        addChange(fight.modes, player.human().fighter().combat().mode());
        // The hash takes both clips, the target's health and the player's feet to the millimetre.
        const std::array<std::int32_t, 6> words{
            static_cast<std::int32_t>(playerClip),
            static_cast<std::int32_t>(targetClip),
            target.health().value(),
            static_cast<std::int32_t>(std::lround(player.human().position().x * 1000.0F)),
            static_cast<std::int32_t>(std::lround(player.human().position().y * 1000.0F)),
            static_cast<std::int32_t>(std::lround(player.human().position().z * 1000.0F))};
        fight.hash = fnv1a(fight.hash, words.data(), sizeof words);
    }
    fight.damage = target.damageTaken();
    fight.hits = target.hitsTaken();
    fight.reactions = target.reactions();
    fight.stuns = target.stuns();
    fight.knockdowns = target.knockdowns();
    fight.power = player.human().fighter().combat().power().value();
    // The clip ids each side played, then the counts.
    std::string playerList;
    for (const std::uint32_t clip : fight.playerClips) {
        playerList += " " + std::to_string(clip);
    }
    std::string targetList;
    for (const std::uint32_t clip : fight.targetClips) {
        targetList += " " + std::to_string(clip);
    }
    std::printf("combat %s: player clips%s\ncombat %s: target clips%s\n", label, playerList.c_str(), label,
                targetList.c_str());
    std::printf("combat %s: %llu frames, damage %d, hits %d, reactions %d, stuns %d, knockdowns %d, health %d/%d, "
                "power %d, hash %016llx\n",
                label, static_cast<unsigned long long>(frames), fight.damage, fight.hits, fight.reactions, fight.stuns,
                fight.knockdowns, target.health().value(), target.health().maximum(), fight.power,
                static_cast<unsigned long long>(fight.hash));
    return fight;
}

// The yard, or null when CONEY_DISC is not set (the test then skips).
const char* discPath() {
    const char* path = SDL_getenv("CONEY_DISC");
    return path != nullptr && *path != '\0' ? path : nullptr;
}

} // namespace

TEST_CASE("on the disc, Rembrandt has the combat clips the fighter and the target play", "[sandbox][combat][disc]") {
    const char* path = discPath();
    if (path == nullptr) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Yard yard = loadYard(path);
    // Every id Coney's fighting plays, on either side.
    std::vector<std::uint32_t> ids{0,   1,   2,   4,   5,   6,   11,  12,  13,  14,  15,  16,  17,  19,  24,
                                   25,  27,  29,  51,  52,  53,  54,  55,  56,  57,  58,  63,  64,  69,  71,
                                   72,  73,  78,  79,  80,  81,  82,  83,  84,  85,  94,  95,  147, 148, 149,
                                   150, 151, 152, 153, 154, 193, 195, 196, 199, 207, 210, 212, 338, 339, 340,
                                   341, 342, 343, 344, 345, 356, 357, 358, 389, 606, 607, 643};
    for (int id = 268; id <= 315; ++id) {
        ids.push_back(static_cast<std::uint32_t>(id));
    }
    int missing = 0;
    int knockdowns = 0;
    std::string missingList;
    for (const std::uint32_t id : ids) {
        const coney::anim::AnimClip* clip = yard.character->anims().clip(id);
        if (clip == nullptr) {
            ++missing;
            missingList += " " + std::to_string(id);
            continue;
        }
        if (std::ranges::any_of(clip->events, [](const coney::anim::ClipEvent& e) { return e.type == 7; })) {
            ++knockdowns;
        }
    }
    std::printf("combat clips: %zu ids, %d missing%s, %d with a knockdown event\n", ids.size(), missing,
                missingList.c_str(), knockdowns);
    CHECK(missing == 0);
}

TEST_CASE("on the disc, a combo hits the target three times and a cross chain twice more", "[sandbox][combat][disc]") {
    const char* path = discPath();
    if (path == nullptr) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Yard yard = loadYard(path);
    const Fight fight = runFight(yard, "combo", "combat_combo.txt", 140);
    CHECK(inOrder(fight.playerClips, {12, 16, 19, 11, 13}));
    CHECK(fight.hits == 5);
    CHECK(fight.reactions == 5);
    CHECK(fight.stuns >= 1);
}

TEST_CASE("on the disc, a grab strikes, spins to the rear and back, and throws", "[sandbox][combat][disc]") {
    const char* path = discPath();
    if (path == nullptr) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Yard yard = loadYard(path);
    const Fight fight = runFight(yard, "grab", "combat_grab.txt", 180);
    CHECK(inOrder(fight.playerClips, {71, 72, 82, 78, 84, 80, 82, 147}));
    CHECK(inOrder(fight.targetClips, {73, 83, 79, 85, 81, 83, 148, 196}));
    CHECK(fight.hits == 2);
    CHECK(std::ranges::find(fight.targetStates, TargetState::Held) != fight.targetStates.end());
    CHECK(fight.targetStates.back() == TargetState::Grounded);
    CHECK(fight.power < 400); // spent by the strike and the throw, drained in the hold, refilling since
}

TEST_CASE("on the disc, a tackle mounts the target and square strikes it", "[sandbox][combat][disc]") {
    const char* path = discPath();
    if (path == nullptr) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Yard yard = loadYard(path);
    const Fight fight = runFight(yard, "tackle", "combat_tackle.txt", 120);
    // Mounted, square strikes with 219 or 221 at random (the victim 220 or 222) and returns to 210.
    CHECK((inOrder(fight.playerClips, {4, 5, 219, 210}) || inOrder(fight.playerClips, {4, 5, 221, 210})));
    CHECK((inOrder(fight.targetClips, {6, 207, 220}) || inOrder(fight.targetClips, {6, 207, 222})));
    CHECK(std::ranges::find(fight.targetStates, TargetState::Mounted) != fight.targetStates.end());
    CHECK(fight.hits == 1);
}

TEST_CASE("on the disc, triangle in a grab starts the mugging", "[sandbox][combat][disc]") {
    const char* path = discPath();
    if (path == nullptr) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Yard yard = loadYard(path);
    const Fight fight = runFight(yard, "mug", "combat_mug.txt", 120);
    CHECK(inOrder(fight.playerClips, {71, 82, 78, 338}));
    CHECK(inOrder(fight.targetClips, {83, 79, 339}));
    CHECK(std::ranges::find(fight.modes, coney::combat::CombatMode::Mugging) != fight.modes.end());
}

TEST_CASE("on the disc, the holds 82-85 pose the pelvis as the game does at runtime", "[sandbox][combat][disc]") {
    const char* path = discPath();
    if (path == nullptr) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Yard yard = loadYard(path);
    // The model-space pelvis read in PCSX2 at the holds' first frame (docs/research/combat.md, the grab pose at
    // runtime, #grab-pose-runtime): the grabber's 82 turned 71° about the vertical, the victim's 83 leaning 39°, and
    // the rear holds 84 and 85; none rolled onto its side.
    struct Expected {
        std::uint32_t id;
        coney::anim::Quat rotation;
        float height;
    };
    const std::array<Expected, 4> holds{Expected{82, coney::anim::Quat{-0.701F, -0.034F, 0.056F, 0.711F}, 0.871F},
                                        Expected{83, coney::anim::Quat{-0.693F, -0.252F, 0.589F, 0.332F}, 0.831F},
                                        Expected{84, coney::anim::Quat{-0.329F, -0.645F, 0.537F, 0.433F}, 1.013F},
                                        Expected{85, coney::anim::Quat{-0.282F, -0.662F, 0.454F, 0.525F}, 1.011F}};
    for (const Expected& hold : holds) {
        const coney::anim::AnimClip* clip = yard.character->anims().clip(hold.id);
        REQUIRE(clip != nullptr);
        const coney::anim::Pose pose = coney::anim::samplePose(*clip, 0.0F, coney::anim::referenceRotations());
        const auto bones = coney::anim::boneTransforms(yard.character->skeleton(), pose);
        const coney::anim::Quat pelvis = coney::anim::quatFromMatrix(bones[1]);
        const float dot = std::min(1.0F, std::fabs(coney::anim::dot(pelvis, hold.rotation)));
        const float degrees = 2.0F * std::acos(dot) * 180.0F / std::numbers::pi_v<float>;
        std::printf("hold %u: pelvis (%.3f, %.3f, %.3f, %.3f), %.1f degrees from the runtime one; height %.3f m "
                    "(runtime %.3f)\n",
                    hold.id, static_cast<double>(pelvis.x), static_cast<double>(pelvis.y),
                    static_cast<double>(pelvis.z), static_cast<double>(pelvis.w), static_cast<double>(degrees),
                    static_cast<double>(bones[1].t.z), static_cast<double>(hold.height));
        CHECK(degrees < 3.0F);
        CHECK(std::fabs(bones[1].t.z - hold.height) < 0.02F);
    }
}
