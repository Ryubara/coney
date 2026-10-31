// SPDX-License-Identifier: GPL-3.0-or-later
// The user's folders SDL3 reports, which Coney Studio's default projects folder is made from
// (docs/guides/studio/editor-experience.md#projects-folder).
#include "platform/user_folders.h"

#include <catch2/catch_test_macros.hpp>

using coney::platform::UserFolder;

TEST_CASE("the platform reports absolute user folders, or none", "[user_folders]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none). A machine may have no documents folder (a bare Linux account), so a missing one is allowed; one that
    // is reported is a whole path, which the projects folder is made inside.
    for (const UserFolder which : {UserFolder::Home, UserFolder::Documents}) {
        const auto folder = coney::platform::userFolder(which);
        if (folder) {
            CHECK(folder->is_absolute());
            CHECK_FALSE(folder->empty());
        }
    }
}
