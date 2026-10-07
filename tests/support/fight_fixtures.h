// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cstdint>
#include <map>
#include <memory>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/combat_script.h"
#include "combat/commands.h"
#include "core/pad.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/target_human.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

// The fight the human combat tests stage: a synthetic character with the locomotion and combat clips, the range data
// the research measured, and a player and one target on a floor stepped together by an input script through the
// street's command tables.

namespace coney::test {

inline constexpr anim::Vec3 kAlongY{0.0F, 1.0F, 0.0F};

// The range data the tests fight with: the damage measured on a civilian (combat.md#damage-table), the hit codes and
// flags read at runtime (combat.md#hit-codes), a reach of 1 m (far 1.25 m) for every attack, and the grab's and the
// tackle's far ranges and the grab's places; the strong grapple's strikes are placed as the plain grab's connects.
inline combat::AnimRangeList fightRanges() {
    struct Move {
        std::int16_t damage;
        std::int16_t code;
        std::uint16_t flags;
    };
    const std::map<int, Move> moves{
        {11, {26, 0x09, 0x800}},  {12, {17, 0x0a, 0x800}},  {13, {53, 0x1b, 0}},  {14, {44, 0x2b, 0}},
        {15, {44, 0x1a, 0xc00}},  {16, {36, 0x0b, 0}},      {17, {61, 0x25, 0}},  {19, {53, 0x26, 0xc00}},
        {51, {57, 0, 0}},         {53, {57, 0, 0}},         {55, {57, 0, 0}},     {57, {57, 0, 0}},
        {59, {79, 0, 0}},         {147, {66, 0x2a, 0x100}}, {193, {20, 0x0a, 0}}, {212, {30, 0x06, 0}},
        {0, {31, 0x36, 0}},       {76, {30, 0, 0}},         {96, {0, 0, 0}},      {100, {20, 0x2a, 0x100}},
        {104, {20, 0x26, 0x400}}, {219, {61, 0, 0}},        {221, {61, 0, 0}},    {223, {61, 0, 0}},
        {225, {61, 0, 0}},        {657, {60, 0, 0}},        {659, {60, 0, 0}},    {25, {31, 0, 0}},
        {27, {31, 0, 0}},         {29, {31, 0, 0}}};
    test::Bytes bytes;
    bytes.u32(722);
    for (int animId = 0; animId < 722; ++animId) {
        const auto found = moves.find(animId);
        const Move move = found != moves.end() ? found->second : Move{0, 0, 0};
        std::int16_t far = 0;
        if (animId == combat::anim_id::kGrabIntro) {
            far = 2499;
        } else if (animId == combat::anim_id::kTackleIntro) {
            far = 2999;
        }
        // The grab's records (combat.md#grab-posing): the connecting clips straight ahead at their reach with a far
        // range of 2.5 m, the front and rear holds at their offsets; the snaps to their sides as on the disc
        // (formats/animation.md#anim-range-list); every other id straight ahead at 1 m.
        struct Place {
            std::int16_t x;
            std::int16_t y;
            float reach;
        };
        const std::map<int, Place> places{{72, {0, 1000, 0.999F}},   {74, {0, 1000, 1.018F}},  {82, {351, 936, 1.081F}},
                                          {84, {-399, 916, 0.242F}}, {657, {0, 1000, 0.999F}}, {659, {0, 1000, 1.018F}},
                                          {25, {999, -12, 1.0F}},    {27, {-1000, 0, 1.0F}},   {29, {-39, -999, 1.0F}}};
        const auto placed = places.find(animId);
        const Place place = placed != places.end() ? placed->second : Place{0, 1000, 1.0F};
        if (animId == 72 || animId == 74 || animId == 657 || animId == 659) {
            far = 2500;
        }
        bytes.u16(static_cast<std::uint16_t>(place.x)).u16(static_cast<std::uint16_t>(place.y));
        bytes.u32(std::bit_cast<std::uint32_t>(place.reach)).u16(static_cast<std::uint16_t>(far));
        bytes.u16(static_cast<std::uint16_t>(move.damage)).u16(static_cast<std::uint16_t>(move.code)).u16(move.flags);
    }
    auto list = combat::AnimRangeList::parse(bytes.span());
    REQUIRE(list.has_value());
    return std::move(list).value_or(combat::AnimRangeList{});
}

// The synthetic locomotion and combat clips together.
inline std::vector<test::LocomotionClip> fightClips() {
    std::vector<test::LocomotionClip> clips = test::locomotionClips();
    const std::vector<test::LocomotionClip> fights = test::combatClips();
    clips.insert(clips.end(), fights.begin(), fights.end());
    return clips;
}

// The synthetic character with the locomotion and combat clips (fightClips()), or with `clips`.
struct FightCharacter {
    FightCharacter() : FightCharacter(fightClips()) {}
    explicit FightCharacter(const std::vector<test::LocomotionClip>& clips) : data(test::locomotionData(clips)) {}

    characters::CharacterData data;
    characters::AnimSet anims{data, nullptr};
    combat::AnimRangeList ranges = fightRanges();
};

// A player and one target on a floor, stepped together by an input script through the street's command tables.
class Fight {
  public:
    // The player at (40, 40) facing +y, the target `ahead` metres in front of it, facing it (or `targetHeading`).
    explicit Fight(const FightCharacter& character, float ahead = 1.0F, float sideways = 0.0F,
                   float targetHeading = std::numbers::pi_v<float>)
        : m_mesh(test::makeMesh(test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F))),
          m_human(character.anims, coney::human::AnimSlots::player(), test::identityBind(), 1.0F, &character.ranges),
          m_target(std::make_unique<coney::human::TargetHuman>(
              character.anims, coney::human::AnimSlots::player(), test::identityBind(), 600,
              anim::Vec3{40.0F + sideways, 40.0F + ahead, 0.0F}, targetHeading)) {
        m_human.spawn(m_mesh.get(), anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        m_targets.push_back(m_target.get());
    }

    // Adds another target `ahead` metres in front of the player and `sideways` to its right, facing `targetHeading`;
    // it is stepped with the first. Returns it.
    coney::human::TargetHuman& addTarget(const FightCharacter& character, float ahead, float sideways,
                                         float targetHeading = std::numbers::pi_v<float>) {
        m_others.push_back(std::make_unique<coney::human::TargetHuman>(
            character.anims, coney::human::AnimSlots::player(), test::identityBind(), 600,
            anim::Vec3{40.0F + sideways, 40.0F + ahead, 0.0F}, targetHeading));
        m_targets.push_back(m_others.back().get());
        return *m_others.back();
    }

    // Runs `frames` updates of `script`, calling `each` after every update with its index.
    template <typename Each> void run(std::string_view script, std::uint64_t frames, Each each) {
        for (const test::PadFrame& frame : test::playScript(script, frames)) {
            const combat::CommandId command =
                m_matcher.update(frame.buttons, m_tables, combat::combatTuning().historyHoldSamples);
            const bool trianglePressed = (frame.buttons & pad::kTriangle) != 0 && (m_lastButtons & pad::kTriangle) == 0;
            m_lastButtons = frame.buttons;
            m_human.step(coney::human::HumanInput{.stickX = frame.leftX,
                                                  .stickY = frame.leftY,
                                                  .cameraForward = kAlongY,
                                                  .sprintHeld = (frame.buttons & pad::kL2) != 0,
                                                  .actionPressed = trianglePressed,
                                                  .command = command,
                                                  .buttons = frame.buttons,
                                                  .targets = m_targets},
                         m_mesh.get());
            m_target->step();
            for (const auto& other : m_others) {
                other->step();
            }
            each(m_frame++);
        }
    }
    // Runs `frames` updates of `script`.
    void run(std::string_view script, std::uint64_t frames) {
        run(script, frames, [](std::uint64_t) {});
    }

    coney::human::Human& human() { return m_human; }
    coney::human::TargetHuman& target() { return *m_target; }

  private:
    std::unique_ptr<raycast::CollisionMesh> m_mesh;
    coney::human::Human m_human;
    std::unique_ptr<coney::human::TargetHuman> m_target;
    std::vector<std::unique_ptr<coney::human::TargetHuman>> m_others;
    std::vector<coney::human::Combatant*> m_targets;
    combat::CommandTables m_tables = combat::CommandTables::street();
    combat::CommandMatcher m_matcher;
    std::uint16_t m_lastButtons = 0;
    std::uint64_t m_frame = 0;
};

} // namespace coney::test
