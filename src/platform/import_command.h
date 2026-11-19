// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>

#include "core/options.h"

namespace coney::platform {

/// Runs `coney --import-save PATH [--profiles DIR]`: reads the PS2 or Xbox save at PATH (warriors/save_import.h) and
/// imports each of its profiles into the first free slot of the profiles folder (`--profiles`, else the player's own,
/// profileFolder()), as a new Coney save. Prints one line per profile to `out` (its name and where it was, or why it
/// was left out: a name already used, no free slot, a slot of another version) and a summary; never a profile's
/// contents.
/// The save is only read. Returns the process's exit code: 0 when at least one profile was imported, 1 when none was
/// (the message names the problem).
///
/// docs/guides/running.md#import-save
[[nodiscard]] int runImportCommand(const Options& options, std::FILE* out);

} // namespace coney::platform
