// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/script_patch.h"

namespace coney::patch {

const PatchSection* ScriptPatch::sectionForBase(const Sha256Digest& /*base*/) const { return nullptr; }

const PatchSection* ScriptPatch::sectionForChunk(const Sha256Digest& /*chunk*/) const { return nullptr; }

std::vector<std::string_view> splitLines(std::string_view /*text*/) { return {}; }

Sha256Digest textHash(std::string_view /*text*/) { return {}; }

Sha256Digest linesHash(std::span<const std::string_view> /*lines*/) { return {}; }

std::expected<ScriptPatch, Error> readScriptPatch(std::string_view /*text*/) {
    return fail(ErrorCode::Invalid, "stub");
}

std::string writeScriptPatch(const ScriptPatch& /*patch*/) { return {}; }

std::string hashText(const Sha256Digest& /*digest*/) { return {}; }

} // namespace coney::patch
