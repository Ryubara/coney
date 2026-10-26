// SPDX-License-Identifier: GPL-3.0-or-later
// The --mesh-view option's parsing (docs/guides/running-options.md); the view list it carries is tested in
// tests/debug/mesh_views_test.cpp.
#include "core/options.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {
// Parses a fixed list of arguments, sparing each test the span conversion.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return coney::parseOptions(std::span<const std::string_view>(args));
}
} // namespace

TEST_CASE("the mesh view option takes its list once", "[options]") {
    // Expected: unverified, #902 (docs/guides/running-options.md)
    auto given = parse(std::array<std::string_view, 2>{"--mesh-view", "wire=all,frames=5"});
    REQUIRE(given.has_value());
    CHECK(given->meshView == "wire=all,frames=5");
    CHECK_FALSE(parse(std::array<std::string_view, 0>{})->meshView.has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--mesh-view"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 4>{"--mesh-view", "wire=all", "--mesh-view", "normals"}).has_value());
    CHECK(coney::usageText().find("--mesh-view SPEC") != std::string_view::npos);
}
