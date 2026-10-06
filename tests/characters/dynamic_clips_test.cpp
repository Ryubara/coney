// SPDX-License-Identifier: GPL-3.0-or-later
// The level's dynamic clips (characters/dynamic_clips.h): each name loaded once through the loader, a failure kept,
// an empty name never asked for. Synthetic clips; the disc's are loaded by the Rumble disc tests.
#include "characters/dynamic_clips.h"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "core/error.h"

TEST_CASE("dynamic clips load each name once and keep failures", "[characters][anim]") {
    std::vector<std::string> asked;
    coney::characters::DynamicClips clips(
        [&asked](std::string_view name) -> std::expected<coney::anim::AnimClip, coney::Error> {
            asked.emplace_back(name);
            if (name == "missing.anm") {
                return coney::fail(coney::ErrorCode::NotFound, "no such clip");
            }
            coney::anim::AnimClip clip;
            clip.name = std::string(name);
            clip.duration = 1.5F;
            return clip;
        });
    const coney::anim::AnimClip* cheer = clips.find("cheer.anm");
    REQUIRE(cheer != nullptr);
    CHECK(cheer->name == "cheer.anm");
    CHECK(clips.find("cheer.anm") == cheer);
    CHECK(clips.find("missing.anm") == nullptr);
    CHECK(clips.find("missing.anm") == nullptr);
    CHECK(clips.find("") == nullptr);
    CHECK(asked == std::vector<std::string>{"cheer.anm", "missing.anm"});
    CHECK(clips.failures() == 1);
}
