// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <catch2/catch_test_macros.hpp>

namespace coney::test {

/// The value in `value`, or a default-made one after failing a check when it is empty: a checked read that keeps a
/// test going and that clang-tidy's optional-access check accepts (it does not see a failed REQUIRE as the end).
template <typename T> [[nodiscard]] T got(const std::optional<T>& value) {
    CHECK(value.has_value());
    return value.value_or(T{});
}

} // namespace coney::test
