// SPDX-License-Identifier: GPL-3.0-or-later
// The edit script under script patches (docs/guides/script-patches.md#make). Sequences are made-up numbers.
#include "core/sequence_diff.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using coney::patch::DiffStep;

namespace {

// Replays `steps` on `a`, taking inserted elements from `b` in order; fails the test if a Same step joins two elements
// that differ or the script runs off either sequence.
std::vector<std::uint32_t> replay(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b,
                                  const std::vector<DiffStep>& steps) {
    std::vector<std::uint32_t> out;
    std::size_t i = 0;
    std::size_t j = 0;
    for (const DiffStep step : steps) {
        switch (step) {
        case DiffStep::Same:
            REQUIRE(i < a.size());
            REQUIRE(j < b.size());
            REQUIRE(a[i] == b[j]);
            out.push_back(a[i++]);
            ++j;
            break;
        case DiffStep::Delete:
            REQUIRE(i < a.size());
            ++i;
            break;
        case DiffStep::Insert:
            REQUIRE(j < b.size());
            out.push_back(b[j++]);
            break;
        }
    }
    CHECK(i == a.size());
    CHECK(j == b.size());
    return out;
}

// How many Delete and Insert steps a script has.
std::size_t cost(const std::vector<DiffStep>& steps) {
    std::size_t edits = 0;
    for (const DiffStep step : steps) {
        edits += step == DiffStep::Same ? 0 : 1;
    }
    return edits;
}

// A sequence of `count` numbers below `range` from a fixed linear congruential generator, so every run is the same.
std::vector<std::uint32_t> numbers(std::size_t count, std::uint32_t range, std::uint32_t seed) {
    std::vector<std::uint32_t> out;
    std::uint32_t state = seed;
    for (std::size_t i = 0; i < count; ++i) {
        state = state * 1664525U + 1013904223U;
        out.push_back((state >> 16) % range);
    }
    return out;
}

} // namespace

TEST_CASE("A diff is the shortest edit script for small sequences", "[patch]") {
    // Expected: ../guides/script-patches.md#make, Coney's own format (no counterpart in the original)
    using V = std::vector<std::uint32_t>;
    struct Case {
        V a;
        V b;
        std::size_t edits; // the shortest edit script's length, worked out by hand
    };
    const auto c = GENERATE(values<Case>({
        {{}, {}, 0},
        {{1, 2, 3}, {1, 2, 3}, 0},
        {{}, {1, 2}, 2},
        {{1, 2}, {}, 2},
        {{1, 2, 3}, {1, 3}, 1},
        {{1, 2, 3, 4, 5}, {1, 9, 3, 4, 8}, 4},
        // Myers' own example: ABCABBA to CBABAC takes 5.
        {{1, 2, 3, 1, 2, 2, 1}, {3, 2, 1, 2, 1, 3}, 5},
        {{1, 2, 3, 4}, {3, 4, 1, 2}, 4},
    }));
    const auto steps = coney::patch::diffSequences(c.a, c.b);
    CHECK(replay(c.a, c.b, steps) == c.b);
    CHECK(cost(steps) == c.edits);
}

TEST_CASE("A diff of long sequences replays into the second one", "[patch]") {
    // Expected: ../guides/script-patches.md#make, Coney's own format (no counterpart in the original)
    const auto seed = GENERATE(1U, 2U, 3U);
    const auto a = numbers(2000, 50, seed);
    auto b = a;
    // Some edits scattered through it: changed, removed and new elements.
    for (std::size_t i = 7; i < b.size(); i += 97) {
        b[i] = 1000 + static_cast<std::uint32_t>(i);
    }
    b.erase(b.begin() + 500, b.begin() + 540);
    b.insert(b.begin() + 1200, 25, 7777U);
    const auto steps = coney::patch::diffSequences(a, b);
    CHECK(replay(a, b, steps) == b);
    // 21 changed (two steps each), 40 removed, 25 new: a shortest script is no longer than that.
    CHECK(cost(steps) <= 2 * 21 + 40 + 25);
}

TEST_CASE("A diff past its cost cap still replays into the second sequence", "[patch]") {
    // Expected: ../guides/script-patches.md#make, Coney's own format (no counterpart in the original)
    const auto a = numbers(3000, 1000, 11U);
    const auto b = numbers(3000, 1000, 12U);
    const auto steps = coney::patch::diffSequences(a, b, 16);
    CHECK(replay(a, b, steps) == b);
}
