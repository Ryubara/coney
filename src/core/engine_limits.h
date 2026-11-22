// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The engine limits preset (docs/guides/running-options.md#engine-limits): which rule each lifted limit of the original
// follows in a run. Coney's own rule lifts every fixed limit past the original's range; the *original* preset pins each
// lift back at the original's number, so a run compares with the PS2 one to one and a lift cannot change what a test
// sees unnoticed (docs/guides/conventions.md#original-limits). Every function that carries a `@coney-only limit:`
// marker takes the preset and gives the original's number under it. The preset is chosen once for a run (the options,
// then the game session and the game mode that owns the state) and never changes during play, so a run is the same on
// every machine and every peer of a network game plays the host's.

#include <cstdint>
#include <optional>
#include <string_view>

namespace coney {

/// Which rule the lifted limits follow in a run.
enum class EngineLimits : std::uint8_t {
    Coney,    ///< Coney's own rule past the original's range: every lift on. The default of a run a person plays.
    Original, ///< the engine limits preset: every lift pinned at the original's number. What test mode runs.
};

/// The preset `name` names, as `--engine-limits` takes it: `coney` or `original`; nothing for anything else.
[[nodiscard]] std::optional<EngineLimits> parseEngineLimitsName(std::string_view name);

/// The lower-case name of `limits`: `coney` or `original`.
[[nodiscard]] std::string_view engineLimitsName(EngineLimits limits);

} // namespace coney
