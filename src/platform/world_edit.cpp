// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_edit.h"

#include <utility>

// Stubs: the tests of platform/world_edit.h come first (docs/guides/stamps.md#fails-first); each does nothing or
// returns a default.

namespace coney::platform {

GameMesh gameMeshOf(const rw::Atomic* /*atomic*/) { return {}; }

debug::GameBox gameBoxOf(const world::Box& /*box*/) { return {}; }

WorldEditor::WorldEditor(debug::WorldEdits edits, std::function<void(std::string_view)> print)
    : m_edits(std::move(edits)), m_print(std::move(print)) {}

void WorldEditor::afterStep(std::uint64_t /*step*/, WorldSet& /*worlds*/, raycast::CollisionMesh* /*collision*/) {}

bool WorldEditor::drawsWorld(std::size_t /*world*/) const { return true; }

void WorldEditor::drawAfterWorld(std::size_t /*world*/, SceneLighting& /*lighting*/) {}

std::optional<world::DebugCamera> WorldEditor::viewerCamera() const { return std::nullopt; }

std::string WorldEditor::summary() const { return {}; }

std::expected<std::shared_ptr<WorldEditor>, Error> loadWorldEditor(const std::string& /*path*/,
                                                                   std::function<void(std::string_view)> print) {
    return std::make_shared<WorldEditor>(debug::WorldEdits{}, std::move(print));
}

} // namespace coney::platform
