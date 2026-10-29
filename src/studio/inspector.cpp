// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/inspector.h"

namespace coney::studio {

std::shared_ptr<debug::MenuPage> inspectorPage(const LevelDocument&, std::optional<EntityId>) {
    return std::make_shared<debug::MenuPage>("");
}

} // namespace coney::studio
