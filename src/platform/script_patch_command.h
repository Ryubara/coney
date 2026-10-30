// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <span>
#include <string_view>

namespace coney::platform {

/// Runs `coney --script-patch make|apply ...` (docs/guides/script-patches.md#command-line), given the arguments after
/// `--script-patch`:
///
/// - `make BASE EDITED --out PATCH (--chunk HASH | --chunk-file FILE) [--form FORM] [--name NAME] [--entry 0xHASH]`
///   writes the patch from the base text to the edited copy (patch::makePatch()).
/// - `apply PATCH BASE --out RESULT` writes the patched text, from the patch's section whose base is BASE
///   (patch::applyPatch()).
///
/// Prints one summary line to `out` (counts and the result hash, never a line of either text) and a refusal to `err`.
/// Returns the process's exit code: 0 when the file was written, 1 when a file could not be read or written or the
/// patch was refused, 2 for a usage error.
[[nodiscard]] int runScriptPatchCommand(std::span<const std::string_view> args, std::FILE* out, std::FILE* err);

} // namespace coney::platform
