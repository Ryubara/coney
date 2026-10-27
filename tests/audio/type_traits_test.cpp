// SPDX-License-Identifier: GPL-3.0-or-later
// A character type's traits for the humans' sounds, read from the recorded CfgChar and CfgBreathingSound calls, and
// what the lookup costs (docs/guides/performance.md#spikes-995). Every record here is made up.
#include "audio/type_traits.h"

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/perf_probes.h"
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"

using Catch::Approx;
using coney::audio::HumanTraits;

namespace {

// Records one made-up CfgChar call: type, class (13 a boss), female 0 or 1, and a model name long enough that a copy
// of it allocates.
void recordCfgChar(coney::script::RecordedCalls& recorded, int type, int category, int female) {
    using coney::script::Value;
    const std::vector<Value> args{Value(static_cast<double>(type)),
                                  Value(1.0),
                                  Value(static_cast<double>(category)),
                                  Value(1.0),
                                  Value(1.0),
                                  Value(100.0),
                                  Value(),
                                  Value(),
                                  Value(1.0),
                                  Value(std::string("a_made_up_model_name_") + std::to_string(type)),
                                  Value(std::string("a_made_up_hat_name_long")),
                                  Value(0.0),
                                  Value(static_cast<double>(female))};
    recorded.add("CfgChar", args);
}

// The allocations one typeTraits() lookup of `type` makes.
std::uint64_t allocationsOfLookup(const coney::script::RecordedCalls& recorded, int type) {
    const std::uint64_t before = coney::platform::allocationTotals().allocations;
    const HumanTraits traits = coney::audio::typeTraits(recorded, type);
    const std::uint64_t after = coney::platform::allocationTotals().allocations;
    CHECK(traits.female);
    return after - before;
}

} // namespace

// Expected: docs/guides/performance.md#spikes-995
TEST_CASE("a type's traits come from its last CfgChar record and the last CfgBreathingSound", "[audio]") {
    coney::script::RecordedCalls recorded;
    recordCfgChar(recorded, 7, 13, 0);
    recordCfgChar(recorded, 8, 2, 1);
    recordCfgChar(recorded, 7, 2, 1); // the last record of type 7 wins
    recorded.add("CfgBreathingSound", std::vector<coney::script::Value>{coney::script::Value(2.5)});
    const HumanTraits seven = coney::audio::typeTraits(recorded, 7);
    CHECK(seven.female);
    CHECK_FALSE(seven.bossClass);
    CHECK(seven.canSpeak);
    CHECK(seven.combatFactor == Approx(2.5F));
    recordCfgChar(recorded, 9, 13, 0);
    const HumanTraits nine = coney::audio::typeTraits(recorded, 9);
    CHECK_FALSE(nine.female);
    CHECK(nine.bossClass);
    // A type with no record has the defaults, and the combat factor still.
    const HumanTraits none = coney::audio::typeTraits(recorded, 50);
    CHECK_FALSE(none.female);
    CHECK_FALSE(none.bossClass);
    CHECK(none.combatFactor == Approx(2.5F));
}

// Expected: docs/guides/performance.md#spikes-995
TEST_CASE("looking up a type's traits costs the same however many other types are configured", "[audio][perf]") {
    // Issue 995: every sound a human made parsed every type's CfgChar record, so a crowd's footsteps and blows took
    // tens of thousands of allocations in one step. The lookup now reads only the records of the type asked for.
    coney::script::RecordedCalls few;
    recordCfgChar(few, 1, 2, 1);
    coney::script::RecordedCalls many;
    recordCfgChar(many, 1, 2, 1);
    for (int type = 2; type < 300; ++type) {
        recordCfgChar(many, type, 2, 0);
    }
    // Type 1 is the first record, so the lookup from the last has every other type's record to pass.
    CHECK(allocationsOfLookup(many, 1) <= allocationsOfLookup(few, 1));
}
