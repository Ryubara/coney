// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One Catch2 test case per item of a list, so that ctest sees each as a test of its own and runs them in parallel
// (docs/guides/testing.md#one-test-per-item). A loop inside one case, or a GENERATE, is a single ctest test however
// many items it covers: catch_discover_tests lists the cases the executable registers and cannot see into a case. The
// disc tests that play each checkpoint, mission or AI seed in turn took minutes as one test, and a test is only as
// fast as its longest case. No game data.
//
// This uses two of Catch2's internals, `Catch::AutoReg` and `Catch::ITestInvoker` (the pair TEST_CASE itself expands
// to), because the public macros register a function with no argument. They are as pinned in cmake/deps.cmake
// (Catch2 v3.16.0): an update of the pin that changes them fails this header's build.

#include <cstddef>
#include <functional>
#include <source_location>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_test_invoker.hpp>
#include <catch2/internal/catch_source_line_info.hpp>
#include <catch2/internal/catch_test_registry.hpp>
#include <catch2/internal/catch_unique_ptr.hpp>

namespace coney::test {

namespace detail {

/// Runs one stored body as a test case: what TEST_CASE's function is, with the item the case is for captured.
class CaseInvoker final : public Catch::ITestInvoker {
  public:
    explicit CaseInvoker(std::function<void()> body) : m_body(std::move(body)) {}

    void invoke() const override { m_body(); }

  private:
    std::function<void()> m_body;
};

} // namespace detail

/// Registers a Catch2 test case `name` with `tags` (written as in TEST_CASE: "[disc][story]") that runs `body`. Call it
/// while the program starts, as a namespace-scope initialiser (see registerEach()); the name must not be another
/// test's. Returns true so that it can initialise a constant.
inline bool registerCase(const std::string& name, const std::string& tags, std::function<void()> body,
                         std::source_location where = std::source_location::current()) {
    const Catch::SourceLineInfo line(where.file_name(), static_cast<std::size_t>(where.line()));
    const Catch::AutoReg registration(Catch::Detail::make_unique<detail::CaseInvoker>(std::move(body)), line,
                                      Catch::StringRef(), Catch::NameAndTags(name, tags));
    (void)registration;
    return true;
}

/// Registers one test case per element of `items`, named `nameOf(item)` and tagged `tags`, whose body is `body(item)`.
/// Use it at namespace scope for a case that would loop over its items:
///
///     const bool kRegistered = coney::test::registerEach(
///         kSeeds, "[disc][story]", [](unsigned seed) { return std::format("the chase, AI seed {}", seed); },
///         [](unsigned seed) { playChase(seed); });
///
/// Each item is copied into its case, so `items` may be a temporary; a body skips with SKIP() and checks with REQUIRE()
/// as a TEST_CASE does. Returns true.
template <typename Items, typename NameOf, typename Body>
bool registerEach(const Items& items, const std::string& tags, NameOf nameOf, Body body,
                  std::source_location where = std::source_location::current()) {
    for (const auto& item : items) {
        registerCase(nameOf(item), tags, [item, body] { body(item); }, where);
    }
    return true;
}

} // namespace coney::test
