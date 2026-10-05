// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/locomotion_gate.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

// The locomotion gate's two masks (docs/research/tasks.md#locomotion-gate): which record +0x08 bits make a human busy
// (no stick step at all) and which zero the stick's velocity, and the state code's 5 and 6.

using coney::human::GateInput;
using coney::human::stickBusy;
using coney::human::stickVelocityGated;

namespace {

// The gate's input with only `flags` set.
GateInput withFlags(std::uint32_t flags) {
    return GateInput{.flags = flags, .stateCode = 0, .airborne = false, .attached = false};
}

} // namespace

TEST_CASE("an attack's phases, the grab bit, the duck and the run stop make a human busy; the recovery, the landing, "
          "a start clip and 389 do not",
          "[human][gate]") {
    for (const std::uint32_t busy : {0x1U, 0x2U, 0x4U, 0x10U, 0x1000U, 0x2000U, 0x80000U}) {
        INFO("bit " << busy);
        CHECK(stickBusy(withFlags(busy)));
    }
    for (const std::uint32_t free : {0x0U, 0x800U, 0x40000U, 0x100000U, 0x1000000U, 0x10000000U, 0x40000000U}) {
        INFO("bit " << free);
        CHECK_FALSE(stickBusy(withFlags(free)));
    }
    // Every bit outside the six counts.
    for (int bit = 0; bit < 32; ++bit) {
        const std::uint32_t flag = 1U << static_cast<unsigned>(bit);
        CHECK(stickBusy(withFlags(flag)) == ((flag & coney::human::kBusyFlags) != 0));
    }
    CHECK(coney::human::kBusyFlags == 0xaeebf7ffU);
    // In the air or held in a grab the stick step is skipped too.
    CHECK(stickBusy(GateInput{.flags = 0, .stateCode = 0, .airborne = true, .attached = false}));
    CHECK(stickBusy(GateInput{.flags = 0, .stateCode = 0, .airborne = false, .attached = true}));
}

TEST_CASE("the recovery, the run stop, the landing and a start clip zero the stick's velocity, as do state codes 5 "
          "and 6",
          "[human][gate]") {
    for (const std::uint32_t gated : {0x80U, 0x800U, 0x40000U, 0x80000U, 0x1000000U, 0x10000000U}) {
        INFO("bit " << gated);
        CHECK(stickVelocityGated(withFlags(gated)));
    }
    for (const std::uint32_t open : {0x0U, 0x1U, 0x2U, 0x4U, 0x10U, 0x40000000U}) {
        INFO("bit " << open);
        CHECK_FALSE(stickVelocityGated(withFlags(open)));
    }
    CHECK(coney::human::kVelocityGateFlags == 0x110c0880U);
    CHECK(stickVelocityGated(GateInput{.flags = 0, .stateCode = 5, .airborne = false, .attached = false}));
    CHECK(stickVelocityGated(GateInput{.flags = 0, .stateCode = 6, .airborne = false, .attached = false}));
    CHECK_FALSE(stickVelocityGated(GateInput{.flags = 0, .stateCode = 0xd, .airborne = false, .attached = false}));
}
