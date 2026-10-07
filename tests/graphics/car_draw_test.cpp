// SPDX-License-Identifier: GPL-3.0-or-later
// Which of a car model's atomics draw in which pass (docs/research/graphics.md#car-draw).
#include "graphics/car_draw.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using coney::graphics::carAtomicDraws;
using coney::graphics::carPartGlass;
using coney::graphics::carPartOf;
using coney::graphics::carPartPainted;
using coney::graphics::CarPass;

namespace {

// A car with no damage.
constexpr std::array<float, 26> kWhole{};

} // namespace

TEST_CASE("The paint tints the body, bonnet, boot, side panels and doors only") {
    for (const std::size_t part : {0, 4, 5, 10, 11, 12, 13, 14, 16, 18, 20}) {
        CHECK(carPartPainted(part));
    }
    // The roof, bumpers, glass, lights and wheels keep their texture's colours.
    for (const std::size_t part : {1, 2, 3, 6, 7, 8, 9, 15, 17, 19, 21, 22, 23, 24, 25}) {
        CHECK_FALSE(carPartPainted(part));
    }
}

TEST_CASE("The windscreen, the back window and the door windows are the glass") {
    for (std::size_t part = 0; part < 26; ++part) {
        const bool glass = part == 6 || part == 7 || part == 15 || part == 17 || part == 19 || part == 21;
        CHECK(carPartGlass(part) == glass);
    }
}

TEST_CASE("Each part draws in its own pass") {
    CHECK(carAtomicDraws(0, 0, kWhole, CarPass::Opaque));
    CHECK_FALSE(carAtomicDraws(0, 0, kWhole, CarPass::Glass));
    CHECK(carAtomicDraws(6, 0, kWhole, CarPass::Glass));
    CHECK_FALSE(carAtomicDraws(6, 0, kWhole, CarPass::Opaque));
}

TEST_CASE("A part swaps to its damaged form at half damage") {
    std::array<float, 26> damage{};
    CHECK(carPartOf(29) == 4);
    CHECK(carAtomicDraws(4, 0, damage, CarPass::Opaque));
    CHECK_FALSE(carAtomicDraws(29, 0, damage, CarPass::Opaque));
    damage[4] = 0.49F;
    CHECK(carAtomicDraws(4, 0, damage, CarPass::Opaque));
    damage[4] = 0.5F;
    CHECK_FALSE(carAtomicDraws(4, 0, damage, CarPass::Opaque));
    CHECK(carAtomicDraws(29, 0, damage, CarPass::Opaque));
    // A damaged window is still glass.
    damage[15] = 1.0F;
    CHECK(carAtomicDraws(40, 0, damage, CarPass::Glass));
    CHECK_FALSE(carAtomicDraws(40, 0, damage, CarPass::Opaque));
}

TEST_CASE("The wheels always draw undamaged") {
    std::array<float, 26> damage{};
    damage[22] = 1.0F;
    CHECK(carAtomicDraws(22, 0, damage, CarPass::Opaque));
}

TEST_CASE("A removed part draws in neither form") {
    std::array<float, 26> damage{};
    damage[5] = 1.0F;
    const std::uint32_t removed = 1U << 5U;
    CHECK_FALSE(carAtomicDraws(5, removed, damage, CarPass::Opaque));
    CHECK_FALSE(carAtomicDraws(30, removed, damage, CarPass::Opaque));
    CHECK(carAtomicDraws(4, removed, damage, CarPass::Opaque));
}
