// SPDX-License-Identifier: GPL-3.0-or-later

// The shared helper that registers one Catch2 test case per item (support/each_case.h), so that ctest runs the items
// as tests of their own in parallel.

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_capture.hpp>
#include <catch2/interfaces/catch_interfaces_registry_hub.hpp>
#include <catch2/interfaces/catch_interfaces_testcase.hpp>

#include "support/each_case.h"

namespace {

constexpr std::array kProbes{3, 5, 8};

// The probe's name as the registration writes it.
std::string probeName(int item) { return std::format("each_case probe: item {}", item); }

// One case per probe item. Each runs by itself under ctest, so the check below that it was given its own item is made
// by a run the registration really made, not by a call of the body in the test that lists them.
[[maybe_unused]] const bool kRegistered =
    coney::test::registerEach(kProbes, "[each_case][each_case_probe]", probeName, [](int item) {
        CHECK(Catch::getResultCapture().getCurrentTestName() == probeName(item));
        CHECK(std::ranges::find(kProbes, item) != kProbes.end());
    });

} // namespace

TEST_CASE("registerEach registers one test case per item, named and tagged as asked", "[each_case]") {
    std::array<bool, kProbes.size()> found{};
    for (const Catch::TestCaseHandle& test : Catch::getRegistryHub().getTestCaseRegistry().getAllTests()) {
        const Catch::TestCaseInfo& info = test.getTestCaseInfo();
        for (std::size_t i = 0; i < kProbes.size(); ++i) {
            if (info.name == probeName(kProbes[i])) {
                found[i] = true;
                const auto hasTag = [&info](std::string_view tag) {
                    return std::ranges::any_of(info.tags, [tag](const Catch::Tag& t) {
                        return std::string_view(t.original.data(), t.original.size()) == tag;
                    });
                };
                CHECK(hasTag("each_case"));
                CHECK(hasTag("each_case_probe"));
                // The caller's own file and line, so a failure points at the registration.
                CHECK(std::string_view(info.lineInfo.file).ends_with("each_case_test.cpp"));
            }
        }
    }
    CHECK(std::ranges::all_of(found, [](bool seen) { return seen; }));
}
