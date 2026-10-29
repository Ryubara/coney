// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/level_document.h"

namespace coney::studio {

std::string_view entityKindName(EntityKind) { return {}; }

LevelDocument LevelDocument::fromLayout(sandbox::SandboxLayout, std::filesystem::path) { return LevelDocument(); }

std::expected<LevelDocument, Error> LevelDocument::load(const std::filesystem::path&) {
    return fail(ErrorCode::Io, "not written yet");
}

std::string LevelDocument::title() const { return {}; }

const Entity* LevelDocument::entity(EntityId) const { return nullptr; }

const sandbox::Primitive* LevelDocument::primitive(const Entity&) const { return nullptr; }

std::optional<EntityId> LevelDocument::entityOfPrimitive(std::size_t) const { return std::nullopt; }

Bounds LevelDocument::bounds(const Entity&) const { return {}; }

} // namespace coney::studio
