// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the throw-aim arc's texture, `aimthrow` in the WAD file named by the decimal
// CRC-32 of `aimthrow_tex` (docs/research/graphics.md#throw-arc). It runs only when CONEY_DISC names the disc and
// skips otherwise. It prints counts only, never data (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "core/name_hash.h"
#include "graphics/throw_arc.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "support/disc_play_fixtures.h"

TEST_CASE("the disc's aimthrow texture is one 32 by 64 texture of three palette colours", "[disc][throw_arc]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    // The file the arc loads: named by the decimal CRC-32 of its texture's name, as the renderer looks it up.
    const std::string fileName = std::to_string(coney::crc32(coney::graphics::kThrowArcTexture));
    CHECK(fileName == "646320484");
    auto entry = wad->lookup(fileName);
    REQUIRE(entry.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(table);
    auto dictionaries = coney::platform::loadTextureDictionaries(*wad, **entry, table);
    REQUIRE(dictionaries.has_value());
    REQUIRE(dictionaries->size() == 1);
    auto images = dictionaries->front().toImages();
    REQUIRE(images.has_value());
    REQUIRE(images->size() == 1);
    const coney::platform::RgbaImage& image = images->front();
    CHECK(image.name == "aimthrow");
    CHECK(image.width == 32);
    CHECK(image.height == 64);
    REQUIRE(image.pixels.size() == 32U * 64U * 4U);

    // The texels by alpha (0 transparent, 255 opaque, between faint) and how many have each RGBA colour (alpha, red,
    // green, blue in one word).
    std::size_t transparent = 0;
    std::size_t faint = 0;
    std::size_t opaque = 0;
    std::map<std::uint32_t, std::size_t> colours;
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        const std::uint8_t alpha = image.pixels[i + 3];
        if (alpha == 0) {
            ++transparent;
        } else if (alpha >= 255) {
            ++opaque;
        } else {
            ++faint;
        }
        ++colours[(static_cast<std::uint32_t>(alpha) << 24U) | (static_cast<std::uint32_t>(image.pixels[i]) << 16U) |
                  (static_cast<std::uint32_t>(image.pixels[i + 1]) << 8U) | image.pixels[i + 2]];
    }
    std::printf("aimthrow: %zu texels transparent, %zu faint, %zu opaque, %zu distinct colours\n", transparent, faint,
                opaque, colours.size());
    // Three palette entries (RGBA as librw converts them): 640 texels (31 %) transparent black, 128 (6 %) faint light
    // grey, 1,280 (62 %) the same grey opaque. The mean of a filtered texel's colour, (1,280 + 128) / 2,048 of 150, is
    // about 103: 0.40 of the vertex colour that the measured arc shows when 255 is 1.0.
    CHECK(colours.size() == 3);
    CHECK(transparent == 640);
    CHECK(faint == 128);
    CHECK(opaque == 1280);
    CHECK(colours[0x00000000U] == 640);
    CHECK(colours[0x1b969696U] == 128);
    CHECK(colours[0xff969696U] == 1280);
}
