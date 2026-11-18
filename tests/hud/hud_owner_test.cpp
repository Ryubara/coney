// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD owner (docs/guides/network-architecture.md#sub-hud): a play mode steps its own HUD until it is handed the
// game's, on synthetic data.
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "hud/hud.h"
#include "hud/hud_owner.h"

using coney::hud::Hud;
using coney::hud::HudOwner;

// Expected: docs/guides/network-architecture.md#sub-hud (Coney's own HUD owner)
TEST_CASE("a HUD owner steps its own HUD until it is handed the game's, which it then steps instead") {
    HudOwner owner;
    Hud& own = owner.hud();
    CHECK(&std::as_const(owner).hud() == &own);

    Hud shared;
    owner.useHud(shared);
    CHECK(&owner.hud() == &shared);
    CHECK(&std::as_const(owner).hud() == &shared);

    // What is done to the HUD through the owner lands on the game's HUD, not on the owner's own.
    CHECK(owner.hud().attachPlayer(0, 32) == 0);
    CHECK(shared.panel(0).attached());
    CHECK_FALSE(own.panel(0).attached());
}

// Expected: docs/guides/network-architecture.md#sub-hud (Coney's own HUD owner)
TEST_CASE("a HUD owner handed another HUD later steps the newest one") {
    HudOwner owner;
    Hud first;
    Hud second;
    owner.useHud(first);
    owner.useHud(second);
    CHECK(&owner.hud() == &second);
}
