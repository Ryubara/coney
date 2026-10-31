// SPDX-License-Identifier: GPL-3.0-or-later
// Coney Studio's default projects folder, chosen from made-up user folders so no test depends on the machine it runs
// on (docs/guides/studio/editor-experience.md#projects-folder).
#include "studio/projects_folder.h"

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/fixtures.h"
#include "support/optional_value.h"

using coney::studio::defaultProjectsFolder;
using coney::studio::ensureFolderExists;
using coney::studio::UserFolders;

namespace fs = std::filesystem;

TEST_CASE("the default projects folder is Coney Studio/Projects in the documents folder of each platform", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the issue's rule,
    // SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS) joined with `Coney Studio/Projects`; the three platforms' documents
    // folders as SDL reports them: Windows and macOS `Documents`, Linux the XDG directory (which the user may have
    // renamed or translated).
    const coney::test::TempDir fake;
    const fs::path windows = fake.path() / "Users" / "ana" / "Documents";
    const fs::path mac = fake.path() / "Users" / "bo" / "Documents";
    const fs::path linuxXdg = fake.path() / "home" / "cy" / "Dokumente";
    for (const fs::path& documents : {windows, mac, linuxXdg}) {
        INFO(documents.string());
        const auto folder = defaultProjectsFolder(UserFolders{.documents = documents, .home = documents.parent_path()});
        REQUIRE(folder.has_value());
        CHECK(coney::test::got(folder) == documents / "Coney Studio" / "Projects");
    }
}

TEST_CASE("without a documents folder the default projects folder is in the home folder", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the issue's fallback, "the home folder ...
    // when there is none".
    const coney::test::TempDir fake;
    const fs::path home = fake.path() / "home" / "dee";
    const auto folder = defaultProjectsFolder(UserFolders{.documents = std::nullopt, .home = home});
    REQUIRE(folder.has_value());
    CHECK(coney::test::got(folder) == home / "Coney Studio" / "Projects");
}

TEST_CASE("the documents folder wins over home, and with neither there is no default", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    const fs::path documents = fs::path("docs-root") / "Documents";
    const fs::path home = fs::path("home-root");
    CHECK(defaultProjectsFolder(UserFolders{.documents = documents, .home = home}) ==
          documents / "Coney Studio" / "Projects");
    CHECK_FALSE(defaultProjectsFolder(UserFolders{}).has_value());
    // An empty path is no folder either: it would put `Coney Studio` in whatever folder the program runs in.
    CHECK_FALSE(defaultProjectsFolder(UserFolders{.documents = fs::path(), .home = fs::path()}).has_value());
}

TEST_CASE("a trailing separator on the folder the platform reports changes nothing", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none). SDL ends the folders it reports with a separator (`C:\Users\ana\Documents\`).
    const coney::test::TempDir fake;
    const fs::path documents = fake.path() / "Documents";
    const fs::path withSeparator = documents / "";
    const auto folder = defaultProjectsFolder(UserFolders{.documents = withSeparator, .home = std::nullopt});
    REQUIRE(folder.has_value());
    CHECK(coney::test::got(folder) == documents / "Coney Studio" / "Projects");
    CHECK(coney::test::got(folder).filename() == "Projects");
}

TEST_CASE("the projects folder is made with the folders above it on first use, and only then", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    const coney::test::TempDir fake;
    const fs::path folder = fake.path() / "Documents" / "Coney Studio" / "Projects";
    REQUIRE_FALSE(fs::exists(folder));
    REQUIRE(ensureFolderExists(folder).has_value());
    CHECK(fs::is_directory(folder));
    // A second use finds it, and leaves what is in it alone.
    std::ofstream(folder / "kept.txt") << "x";
    REQUIRE(ensureFolderExists(folder).has_value());
    CHECK(fs::is_regular_file(folder / "kept.txt"));
}

TEST_CASE("the projects folder cannot be made where a file is in the way", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    const coney::test::TempDir fake;
    std::ofstream(fake.path() / "Documents") << "a file, not a folder";
    const auto made = ensureFolderExists(fake.path() / "Documents" / "Coney Studio" / "Projects");
    REQUIRE_FALSE(made.has_value());
    CHECK(made.error().code == coney::ErrorCode::Io);
}
