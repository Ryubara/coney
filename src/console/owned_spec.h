// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "console/command_spec.h"

// @coney-only the Coney console (docs/guides/console.md); the original has no console.

namespace coney::console {

/// A CommandSpec that owns its text: the registry's copy of a command, so a mod's command, whose spec comes from a
/// script at run time, lives as long as its registration. Not copyable or movable, since spec() views its own
/// storage; the registry holds each by pointer.
class OwnedSpec {
  public:
    /// A deep copy of `spec`: its strings, arguments and choices.
    explicit OwnedSpec(const CommandSpec& spec);

    OwnedSpec(const OwnedSpec&) = delete;
    OwnedSpec& operator=(const OwnedSpec&) = delete;
    OwnedSpec(OwnedSpec&&) = delete;
    OwnedSpec& operator=(OwnedSpec&&) = delete;
    ~OwnedSpec() = default;

    /// The copy, viewing this object's storage.
    [[nodiscard]] const CommandSpec& spec() const { return m_spec; }

  private:
    // Keeps `text` and returns a view of the kept copy; a deque never moves what it holds.
    std::string_view keep(std::string_view text);

    std::deque<std::string> m_text;
    std::deque<std::vector<std::string_view>> m_choices; // one list per Choice argument
    std::vector<ArgSpec> m_args;
    CommandSpec m_spec;
};

} // namespace coney::console
