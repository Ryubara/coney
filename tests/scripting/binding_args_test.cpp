// SPDX-License-Identifier: GPL-3.0-or-later
// The shared argument readers (scripting/binding_args.h): a default only for an argument past the top of the stack,
// an explicit nil read as 0, and the original's double-to-integer conversions. Synthetic values.
#include "scripting/binding_args.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scripting/lua_value.h"

using coney::script::Value;
namespace binding = coney::script::binding;

namespace {

// 2^32, the first value an unsigned 32-bit integer cannot hold.
constexpr double kTwoTo32 = 4294967296.0;

// One argument list per case the readers are tested on: [0] the value, read at index 0; index 1 is past the top.
std::vector<Value> one(Value value) { return {std::move(value)}; }

} // namespace

TEST_CASE("numberOr gives the default only past the top of the stack, and nil reads 0", "[scripting][binding_args]") {
    // Expected: scripting.md#optional-arguments, confirmed (code)
    CHECK(binding::numberOr(one(Value(1.0)), 1, 7.0) == 7.0);
    CHECK(binding::numberOr(one(Value()), 0, 7.0) == 0.0);
    CHECK(binding::numberOr(one(Value(0.0)), 0, 7.0) == 0.0);
    CHECK(binding::numberOr(one(Value(0.5)), 0, 7.0) == 0.5);
    CHECK(binding::numberOr(one(Value(-1.0)), 0, 7.0) == -1.0);
    CHECK(binding::numberOr(one(Value(kTwoTo32)), 0, 7.0) == kTwoTo32);
    CHECK(binding::numberOr(one(Value(std::string("12.5"))), 0, 7.0) == 12.5);
    CHECK(binding::numberOr(one(Value(std::string("abc"))), 0, 7.0) == 0.0);
}

TEST_CASE("unsignedArg reads as 0x0042c420 converts: negative to 0, 2^32 and above to 0xffffffff",
          "[scripting][binding_args]") {
    // Expected: scripting.md#optional-arguments, confirmed (code)
    CHECK(binding::unsignedArg(one(Value(1.0)), 1, 7.0) == 7U);
    CHECK(binding::unsignedArg(one(Value()), 0, 7.0) == 0U);
    CHECK(binding::unsignedArg(one(Value(0.0)), 0, 7.0) == 0U);
    CHECK(binding::unsignedArg(one(Value(0.5)), 0, 7.0) == 0U);
    CHECK(binding::unsignedArg(one(Value(-1.0)), 0, 7.0) == 0U);
    CHECK(binding::unsignedArg(one(Value(kTwoTo32)), 0, 7.0) == 0xffffffffU);
    CHECK(binding::unsignedArg(one(Value(std::string("12.5"))), 0, 7.0) == 12U);
    CHECK(binding::toUnsigned(kTwoTo32 - 1.0) == 0xffffffffU);
    CHECK(binding::toUnsigned(3.0e9) == 3000000000U);
    CHECK(binding::toUnsigned(std::numeric_limits<double>::quiet_NaN()) == 0U);
    CHECK(binding::toUnsigned(std::numeric_limits<double>::infinity()) == 0xffffffffU);
    CHECK(binding::toUnsigned(-std::numeric_limits<double>::infinity()) == 0U);
}

TEST_CASE("signedArg reads as 0x0042c388 converts: truncation, saturating at the int range",
          "[scripting][binding_args]") {
    // Expected: scripting.md#optional-arguments, confirmed (code)
    constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();
    constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
    CHECK(binding::signedArg(one(Value(1.0)), 1, -1.0) == -1);
    CHECK(binding::signedArg(one(Value()), 0, -1.0) == 0);
    CHECK(binding::signedArg(one(Value(0.0)), 0, -1.0) == 0);
    CHECK(binding::signedArg(one(Value(0.5)), 0, -1.0) == 0);
    CHECK(binding::signedArg(one(Value(-1.0)), 0, -1.0) == -1);
    CHECK(binding::signedArg(one(Value(kTwoTo32)), 0, -1.0) == kMax);
    CHECK(binding::signedArg(one(Value(std::string("12.5"))), 0, -1.0) == 12);
    CHECK(binding::toSigned(-1.5) == -1);
    CHECK(binding::toSigned(2147483647.0) == kMax);
    CHECK(binding::toSigned(2147483648.0) == kMax);
    CHECK(binding::toSigned(-2147483648.0) == kMin);
    CHECK(binding::toSigned(-kTwoTo32) == kMin);
    CHECK(binding::toSigned(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(binding::toSigned(-std::numeric_limits<double>::infinity()) == kMin);
}

TEST_CASE("floatArg gives the default only past the top, and nil reads 0", "[scripting][binding_args]") {
    // Expected: scripting.md#optional-arguments, confirmed (code)
    CHECK(binding::floatArg(one(Value(1.0)), 1, 3.0) == 3.0F);
    CHECK(binding::floatArg(one(Value()), 0, 3.0) == 0.0F);
    CHECK(binding::floatArg(one(Value(0.0)), 0, 3.0) == 0.0F);
    CHECK(binding::floatArg(one(Value(0.5)), 0, 3.0) == 0.5F);
    CHECK(binding::floatArg(one(Value(-1.0)), 0, 3.0) == -1.0F);
    CHECK(binding::floatArg(one(Value(kTwoTo32)), 0, 3.0) == static_cast<float>(kTwoTo32));
    CHECK(binding::floatArg(one(Value(std::string("12.5"))), 0, 3.0) == 12.5F);
}

TEST_CASE("booleanArg reads as 0x00409318: the default when missing, false for nil, else the number is not 0",
          "[scripting][binding_args]") {
    // Expected: scripting.md#optional-arguments, confirmed (code)
    CHECK(binding::booleanArg(one(Value(0.0)), 1, true));
    CHECK_FALSE(binding::booleanArg(one(Value(0.0)), 1, false));
    CHECK_FALSE(binding::booleanArg(one(Value()), 0, true));
    CHECK_FALSE(binding::booleanArg(one(Value(0.0)), 0, true));
    CHECK(binding::booleanArg(one(Value(0.5)), 0, false));
    CHECK(binding::booleanArg(one(Value(-1.0)), 0, false));
    CHECK(binding::booleanArg(one(Value(kTwoTo32)), 0, false));
    CHECK(binding::booleanArg(one(Value(std::string("12.5"))), 0, false));
    CHECK_FALSE(binding::booleanArg(one(Value(std::string("0"))), 0, true));
    CHECK_FALSE(binding::booleanArg(one(Value(std::string("yes"))), 0, true));
}
