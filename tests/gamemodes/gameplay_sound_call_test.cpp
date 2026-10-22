// SPDX-License-Identifier: GPL-3.0-or-later
// The sound call gameplay builds for a human's anim sounds (a mount, a wave or point, a kiyap reach the gesture slots
// through it): it carries the speaker's reaction kind and help level, as Human_OnAnimSoundEvent's caller
// (`0x0021f700`) hands `Ambient_MayGesture` (`0x00291ed0`) the speaker's own (docs/research/sound-events.md).
#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "gamemodes/gameplay_mode.h"
#include "scripting/sound_bindings.h"
#include "support/ai_fixtures.h"

TEST_CASE("gameplay's anim-sound call carries the speaker's reaction kind and help level", "[gamemodes][audio]") {
    coney::test::AiScene scene;
    using coney::ai::BrainType;
    const coney::ai::Brain& player = scene.player();
    const coney::ai::Brain& cop = scene.add(coney::anim::Vec3{41.0F, 40.0F, 0.0F}, 0.0F, BrainType::Cop);
    const coney::ai::Brain& thug = scene.add(coney::anim::Vec3{42.0F, 40.0F, 0.0F}, 0.0F, BrainType::Gang);
    const coney::script::HumanSoundCall copCall = coney::humanSoundCallOf(2.0, cop, &player, false);
    CHECK(copCall.gestureKind == 1);
    CHECK(copCall.gestureSlots == 1);
    const coney::script::HumanSoundCall playerCall = coney::humanSoundCallOf(1.0, player, &player, false);
    CHECK(playerCall.player);
    CHECK(playerCall.gestureKind == 3);
    CHECK(playerCall.gestureSlots == 2);
    const coney::script::HumanSoundCall thugCall = coney::humanSoundCallOf(3.0, thug, &player, false);
    CHECK(thugCall.gestureKind == 2);
    CHECK(thugCall.gestureSlots == 2);
}
