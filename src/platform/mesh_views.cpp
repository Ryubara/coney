// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the tests' commit: nothing is drawn; the next commit implements them.
#include "platform/mesh_views.h"

namespace coney::platform {

MeshViews::~MeshViews() = default;

bool MeshViews::beginFrame(const PlayLevelMode& /*mode*/) { return false; }

void MeshViews::draw(rw::Atomic* /*atomic*/, DrawKind /*kind*/, bool /*layer*/,
                     const std::function<void()>& /*shaded*/) {}

void MeshViewRun::frame(debug::DebugSession& /*session*/) {}

} // namespace coney::platform
