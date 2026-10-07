// SPDX-License-Identifier: GPL-3.0-or-later
// A human's blood pass (docs/research/rendering.md#characters): which texture by health and when it shows.
#include "graphics/human_blood.h"

#include <catch2/catch_test_macros.hpp>

using coney::graphics::bloodLayerFor;
using coney::graphics::BloodTexture;

TEST_CASE("No blood shows from 90% health up") {
    CHECK_FALSE(bloodLayerFor(100.0F).shows);
    CHECK_FALSE(bloodLayerFor(90.0F).shows);
    CHECK(bloodLayerFor(89.9F).shows);
    CHECK(bloodLayerFor(0.0F).shows);
}

TEST_CASE("The blood texture gets heavier at 60% and 30% health") {
    CHECK(bloodLayerFor(100.0F).texture == BloodTexture::Light);
    CHECK(bloodLayerFor(60.0F).texture == BloodTexture::Light);
    CHECK(bloodLayerFor(59.9F).texture == BloodTexture::Medium);
    CHECK(bloodLayerFor(30.0F).texture == BloodTexture::Medium);
    CHECK(bloodLayerFor(29.9F).texture == BloodTexture::Heavy);
    CHECK(bloodLayerFor(0.0F).texture == BloodTexture::Heavy);
    CHECK(coney::graphics::bloodTextureName(BloodTexture::Heavy) == "charblood_d3");
}

TEST_CASE("The no-blood switch hides the blood at any health") { CHECK_FALSE(bloodLayerFor(10.0F, true).shows); }
