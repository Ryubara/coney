// SPDX-License-Identifier: GPL-3.0-or-later
// Coney Studio's per-user preferences: the projects folder setting and the recent projects list, over made-up folders
// and files (docs/guides/studio/editor-experience.md#projects-folder).
#include "studio/studio_preferences.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/fixtures.h"
#include "support/optional_value.h"

using coney::studio::addRecentProject;
using coney::studio::formatPreferences;
using coney::studio::kMaxRecentProjects;
using coney::studio::loadPreferences;
using coney::studio::parsePreferences;
using coney::studio::projectExists;
using coney::studio::RecentProject;
using coney::studio::removeRecentProject;
using coney::studio::savePreferences;
using coney::studio::StudioPreferences;

namespace fs = std::filesystem;

namespace {

// A project entry named after its folder.
RecentProject recent(const fs::path& root) { return RecentProject{.root = root, .name = root.filename().string()}; }

// The roots of `preferences.recent`, in order, as text.
std::vector<std::string> roots(const StudioPreferences& preferences) {
    std::vector<std::string> out;
    out.reserve(preferences.recent.size());
    for (const RecentProject& project : preferences.recent) {
        out.push_back(project.root.generic_string());
    }
    return out;
}

} // namespace

TEST_CASE("recent projects come most recent first, and a reopened one moves to the front", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    StudioPreferences preferences;
    addRecentProject(preferences, recent("projects/a"));
    addRecentProject(preferences, recent("projects/b"));
    addRecentProject(preferences, recent("projects/c"));
    CHECK(roots(preferences) == std::vector<std::string>{"projects/c", "projects/b", "projects/a"});
    // Opening a again puts it first, once, and the name it has now replaces the one it had.
    addRecentProject(preferences, RecentProject{.root = "projects/a", .name = "renamed"});
    CHECK(roots(preferences) == std::vector<std::string>{"projects/a", "projects/c", "projects/b"});
    CHECK(preferences.recent.front().name == "renamed");
}

TEST_CASE("the recent list keeps ten projects and drops the oldest", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    StudioPreferences preferences;
    for (std::size_t i = 0; i < kMaxRecentProjects + 3; ++i) {
        addRecentProject(preferences, recent(fs::path("projects") / ("p" + std::to_string(i))));
    }
    REQUIRE(preferences.recent.size() == kMaxRecentProjects);
    CHECK(kMaxRecentProjects == 10U);
    CHECK(preferences.recent.front().root.filename() == "p12");
    CHECK(preferences.recent.back().root.filename() == "p3"); // p0, p1 and p2 fell off the end
}

TEST_CASE("one folder written two ways is one recent project", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    StudioPreferences preferences;
    addRecentProject(preferences, recent("projects/a"));
    addRecentProject(preferences, recent("projects/b"));
    addRecentProject(preferences, recent("projects/x/../a")); // the same folder as projects/a
    CHECK(preferences.recent.size() == 2U);
    CHECK(preferences.recent.front().root.lexically_normal().generic_string() == "projects/a");
    CHECK(removeRecentProject(preferences, "projects/./b"));
    CHECK(preferences.recent.size() == 1U);
}

TEST_CASE("removing a recent project takes it off the list and says whether it was there", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    StudioPreferences preferences;
    addRecentProject(preferences, recent("projects/a"));
    addRecentProject(preferences, recent("projects/b"));
    CHECK(removeRecentProject(preferences, "projects/a"));
    CHECK(roots(preferences) == std::vector<std::string>{"projects/b"});
    CHECK_FALSE(removeRecentProject(preferences, "projects/a"));
    CHECK_FALSE(removeRecentProject(preferences, "projects/never"));
    CHECK(roots(preferences) == std::vector<std::string>{"projects/b"});
}

TEST_CASE("a project is missing when its folder or its manifest is gone", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    const coney::test::TempDir dir;
    CHECK_FALSE(projectExists(dir.path() / "nothing"));
    fs::create_directories(dir.path() / "empty");
    CHECK_FALSE(projectExists(dir.path() / "empty")); // a folder that is not a project
    fs::create_directories(dir.path() / "yard");
    std::ofstream(dir.path() / "yard" / "coney-project.toml") << "[project]\nname = \"yard\"\n";
    CHECK(projectExists(dir.path() / "yard"));
    fs::remove_all(dir.path() / "yard");
    CHECK_FALSE(projectExists(dir.path() / "yard"));
}

TEST_CASE("preferences read back the same, whatever the paths hold", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    StudioPreferences preferences;
    preferences.projectsFolder = fs::path("D:/My Work/Coney \"projects\"");
    // A quote, a backslash and a letter outside ASCII (UTF-8) in the names and folders.
    preferences.recent.push_back(RecentProject{.root = fs::path("C:/Users/ana/caf\xC3\xA9/yard"), .name = "yard"});
    preferences.recent.push_back(RecentProject{.root = fs::path("/home/bo/a \"b\"/c"), .name = "say \"hi\" \\ bye"});
    const std::string text = formatPreferences(preferences);
    CHECK_FALSE(text.empty());
    const auto read = parsePreferences(text);
    REQUIRE(read.has_value());
    REQUIRE(read->recent.size() == 2U);
    CHECK(read->projectsFolder == preferences.projectsFolder);
    CHECK(read->recent[0].name == "yard");
    CHECK(read->recent[1].name == "say \"hi\" \\ bye");
    CHECK(read->recent[0].root.generic_string() == preferences.recent[0].root.generic_string());
    CHECK(read->recent[1].root.generic_string() == preferences.recent[1].root.generic_string());
    // No folder chosen and no recent projects: still a file that reads back to the defaults.
    const auto empty = parsePreferences(formatPreferences(StudioPreferences{}));
    REQUIRE(empty.has_value());
    CHECK_FALSE(empty->projectsFolder.has_value());
    CHECK(empty->recent.empty());
}

TEST_CASE("preferences text keeps the list's order, skips what it does not know and drops repeats", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    const auto read = parsePreferences("# Coney Studio preferences\n"
                                       "[projects]\n"
                                       "folder = \"projects-root\"   # chosen\n"
                                       "future = 3\n"
                                       "[theme]\n"
                                       "accent = \"#569EFF\"\n"
                                       "[[recent]]\n"
                                       "name = \"first\"\n"
                                       "path = \"p/first\"\n"
                                       "[[recent]]\n"
                                       "name = \"second\"\n"
                                       "path = \"p/second\"\n"
                                       "[[recent]]\n"
                                       "name = \"again\"\n"
                                       "path = \"p/first\"\n"
                                       "[[recent]]\n"
                                       "name = \"no path, so no project\"\n");
    REQUIRE(read.has_value());
    CHECK(read->projectsFolder == fs::path("projects-root"));
    CHECK(roots(*read) == std::vector<std::string>{"p/first", "p/second"});
    CHECK(read->recent[0].name == "first"); // the newer entry (earlier in the file) is the one kept
    // A file with more than ten entries is cut at ten.
    std::string many;
    for (int i = 0; i < 14; ++i) {
        many += "[[recent]]\nname = \"n\"\npath = \"p/" + std::to_string(i) + "\"\n";
    }
    const auto cut = parsePreferences(many);
    REQUIRE(cut.has_value());
    CHECK(cut->recent.size() == kMaxRecentProjects);
    CHECK(cut->recent.front().root.filename() == "0");
}

TEST_CASE("preferences text with a broken line is refused, naming the line", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    const auto noEquals = parsePreferences("[projects]\nfolder\n");
    REQUIRE_FALSE(noEquals.has_value());
    CHECK(noEquals.error().code == coney::ErrorCode::Invalid);
    CHECK(noEquals.error().message.starts_with("line 2:"));
    const auto unterminated = parsePreferences("[projects]\nfolder = \"never closed\n");
    REQUIRE_FALSE(unterminated.has_value());
    CHECK(unterminated.error().message.starts_with("line 2:"));
    CHECK_FALSE(parsePreferences("[projects\n").has_value());
}

TEST_CASE("preferences are saved to a file in a folder made for it and loaded back", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    const coney::test::TempDir dir;
    const fs::path file = dir.path() / "user data" / "coney-studio-preferences.toml";
    StudioPreferences preferences;
    preferences.projectsFolder = dir.path() / "work";
    addRecentProject(preferences, recent(dir.path() / "a"));
    REQUIRE(savePreferences(file, preferences).has_value());
    REQUIRE(fs::is_regular_file(file));
    const auto loaded = loadPreferences(file);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projectsFolder.has_value());
    CHECK(coney::test::got(loaded->projectsFolder).generic_string() ==
          coney::test::got(preferences.projectsFolder).generic_string());
    CHECK(roots(*loaded) == roots(preferences));
    // Saving again replaces the file, and no half-written copy is left beside it.
    preferences.projectsFolder.reset();
    REQUIRE(savePreferences(file, preferences).has_value());
    const auto again = loadPreferences(file);
    REQUIRE(again.has_value());
    CHECK_FALSE(again->projectsFolder.has_value());
    std::size_t files = 0;
    for (const auto& entry : fs::directory_iterator(file.parent_path())) {
        static_cast<void>(entry);
        ++files;
    }
    CHECK(files == 1U);
}

TEST_CASE("a preferences file that is not there is the defaults, and one that is broken is an error", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#projects-folder, the design (Coney's own tooling; the original
    // has none).
    const coney::test::TempDir dir;
    const auto none = loadPreferences(dir.path() / "absent.toml");
    REQUIRE(none.has_value());
    CHECK(*none == StudioPreferences{});
    std::ofstream(dir.path() / "broken.toml") << "this is not a preference\n";
    const auto broken = loadPreferences(dir.path() / "broken.toml");
    REQUIRE_FALSE(broken.has_value());
    CHECK(broken.error().message.find("broken.toml") != std::string::npos);
}
