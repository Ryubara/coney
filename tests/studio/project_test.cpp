// SPDX-License-Identifier: GPL-3.0-or-later
// A Coney Studio project: its manifest and the root rule, that nothing outside the project folder is reachable through
// a `project:/` path (docs/guides/studio/editor-experience.md#project-coney).
#include "studio/project.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "studio/project_manifest.h"
#include "studio/projects_folder.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::studio::normaliseProjectPath;
using coney::studio::parseProjectManifest;
using coney::studio::Project;
using coney::studio::ProjectManifest;

namespace {

// A one-line level, enough for a project to hold.
constexpr std::string_view kLevel = "title Test\nbox at=0,0,0 size=2,2,1\n";

// Writes `text` to `path`, making its folder.
void writeFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
    REQUIRE(out.good());
}

} // namespace

TEST_CASE("a manifest reads the project and play tables and skips what it does not know", "[studio]") {
    // Expected: the fields of the Lua SDK design's example manifest (docs/guides/studio/lua-sdk.md#manifest).
    const auto manifest = parseProjectManifest("# coney-project.toml\n"
                                               "[project]\n"
                                               "name = \"yard-brawl\"   # a comment\n"
                                               "title = \"Yard Brawl\"\n"
                                               "version = \"0.3.0\"\n"
                                               "sdk = 4\n"
                                               "\n"
                                               "[play]\n"
                                               "level = \"project:/levels/yard.layout\"\n"
                                               "spawn = \"start\"\n"
                                               "[scripts]\n"
                                               "server = [\"scripts/main.lua\"]\n");
    REQUIRE(manifest.has_value());
    CHECK(manifest->name == "yard-brawl");
    CHECK(manifest->title == "Yard Brawl");
    CHECK(manifest->version == "0.3.0");
    CHECK(manifest->sdk == 4);
    CHECK(manifest->playLevel == "project:/levels/yard.layout");
    CHECK(manifest->spawn == "start");
}

TEST_CASE("a manifest written by Studio reads back the same", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#project-coney, the project folder and its root rule (design).
    const ProjectManifest manifest{.name = "my-project",
                                   .title = "My \"quoted\" project",
                                   .version = "0.1.0",
                                   .sdk = 1,
                                   .playLevel = "project:/levels/main.layout",
                                   .spawn = ""};
    const auto read = parseProjectManifest(coney::studio::formatProjectManifest(manifest));
    REQUIRE(read.has_value());
    CHECK(*read == manifest);
}

TEST_CASE("a manifest without a valid name or with a broken line is refused with its line", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#project-coney, the project folder and its root rule (design).
    CHECK(parseProjectManifest("[project]\ntitle = \"x\"\n").error().code == ErrorCode::Invalid);
    CHECK(parseProjectManifest("[project]\nname = \"Not Valid\"\n").error().code == ErrorCode::Invalid);
    const auto broken = parseProjectManifest("[project]\nname = \"ok\"\nthis is not toml\n");
    REQUIRE(!broken.has_value());
    CHECK(broken.error().message.starts_with("line 3:"));
    CHECK(!parseProjectManifest("[project]\nname = \"unterminated\n").has_value());
}

TEST_CASE("project names are lower case letters, digits and dashes", "[studio]") {
    // Expected: the manifest's rule for `name` (docs/guides/studio/lua-sdk.md#manifest).
    CHECK(coney::studio::validProjectName("yard-brawl"));
    CHECK(coney::studio::validProjectName("level2"));
    CHECK_FALSE(coney::studio::validProjectName(""));
    CHECK_FALSE(coney::studio::validProjectName("-leading"));
    CHECK_FALSE(coney::studio::validProjectName("Upper"));
    CHECK_FALSE(coney::studio::validProjectName("two words"));
    CHECK_FALSE(coney::studio::validProjectName("dots.no"));
}

TEST_CASE("a project path never climbs above the project's root", "[studio]") {
    // Expected: "no absolute path, no `..` above the root" (docs/guides/studio/editor-experience.md#project-coney).
    CHECK(normaliseProjectPath("project:/").value().empty());
    CHECK(normaliseProjectPath("project:/levels/yard.layout").value() == "levels/yard.layout");
    CHECK(normaliseProjectPath("project:/levels/../textures/./a.png").value() == "textures/a.png");
    CHECK(normaliseProjectPath("project:/levels//a.layout").value() == "levels/a.layout");
    CHECK(normaliseProjectPath("project:/..").error().code == ErrorCode::InvalidArgument);
    CHECK(normaliseProjectPath("project:/levels/../../secret").error().code == ErrorCode::InvalidArgument);
    CHECK(normaliseProjectPath("C:/Windows/win.ini").error().code == ErrorCode::InvalidArgument);
    CHECK(normaliseProjectPath("/etc/passwd").error().code == ErrorCode::InvalidArgument);
    CHECK(normaliseProjectPath("project:/levels\\..\\..\\x").error().code == ErrorCode::InvalidArgument);
    CHECK(normaliseProjectPath("project:/C:/x").error().code == ErrorCode::InvalidArgument);
    CHECK(normaliseProjectPath(std::string("project:/a\0b", 12)).error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("a new project has its folders, its manifest and its first level", "[studio]") {
    const coney::test::TempDir dir;
    const auto root = dir.path() / "yard";
    const auto project = Project::create(root, "yard", "main.layout", kLevel);
    REQUIRE(project.has_value());
    // Expected: the folder of a new project (docs/guides/studio/editor-experience.md#project-coney).
    for (const char* folder : {"levels", "textures", "scripts", ".studio"}) {
        CHECK(std::filesystem::is_directory(root / folder));
    }
    CHECK(std::filesystem::is_regular_file(root / "coney-project.toml"));
    CHECK(std::filesystem::is_regular_file(root / "levels" / "main.layout"));
    CHECK(project->manifest().name == "yard");
    CHECK(project->manifest().playLevel == "project:/levels/main.layout");

    const auto reopened = Project::open(root);
    REQUIRE(reopened.has_value());
    CHECK(reopened->manifest() == project->manifest());
}

TEST_CASE("a project is not made over a folder that holds files, nor opened without a manifest", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#project-coney, the project folder and its root rule (design).
    const coney::test::TempDir dir;
    writeFile(dir.path() / "busy" / "notes.txt", "mine");
    CHECK(Project::create(dir.path() / "busy", "busy", "main.layout", kLevel).error().code ==
          ErrorCode::InvalidArgument);
    CHECK(Project::create(dir.path() / "bad", "Bad Name", "main.layout", kLevel).error().code ==
          ErrorCode::InvalidArgument);
    std::filesystem::create_directories(dir.path() / "empty");
    CHECK(Project::open(dir.path() / "empty").error().code == ErrorCode::NotFound);
}

TEST_CASE("a project resolves its own paths and refuses every path outside it", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#project-coney, the project folder and its root rule (design).
    const coney::test::TempDir dir;
    const auto root = dir.path() / "p";
    const auto project = Project::create(root, "p", "main.layout", kLevel);
    REQUIRE(project.has_value());
    writeFile(dir.path() / "outside.txt", "not the project's");

    CHECK(project->resolve("project:/levels/main.layout").value() == project->root() / "levels" / "main.layout");
    CHECK(project->resolve("project:/../outside.txt").error().code == ErrorCode::InvalidArgument);
    CHECK(project->projectPathOf(project->root() / "levels" / "main.layout").value() == "project:/levels/main.layout");
    CHECK(project->projectPathOf("levels/main.layout").value() == "project:/levels/main.layout");
    CHECK(project->projectPathOf(dir.path() / "outside.txt").error().code == ErrorCode::InvalidArgument);
    CHECK(project->projectPathOf(project->root() / ".." / "outside.txt").error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("the File system lists a project folder, folders first, and nothing above the root", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#project-coney, the project folder and its root rule (design).
    const coney::test::TempDir dir;
    const auto project = Project::create(dir.path() / "p", "p", "main.layout", kLevel);
    REQUIRE(project.has_value());
    writeFile(project->root() / "levels" / "b.layout", kLevel);
    std::filesystem::create_directories(project->root() / "levels" / "zones");

    const auto root = project->list("project:/");
    REQUIRE(root.has_value());
    REQUIRE(root->size() == 5);
    // Folders first by name (".studio" sorts before "levels"), then the manifest.
    CHECK((*root)[0].name == ".studio");
    CHECK((*root)[1].name == "levels");
    CHECK((*root)[4].name == "coney-project.toml");
    CHECK_FALSE((*root)[4].folder);

    const auto levels = project->list("project:/levels");
    REQUIRE(levels.has_value());
    REQUIRE(levels->size() == 3);
    CHECK((*levels)[0].name == "zones");
    CHECK((*levels)[0].folder);
    CHECK((*levels)[1].projectPath == "project:/levels/b.layout");
    CHECK((*levels)[2].projectPath == "project:/levels/main.layout");

    CHECK(project->list("project:/..").error().code == ErrorCode::InvalidArgument);
    CHECK(project->list("project:/levels/main.layout").error().code == ErrorCode::NotFound);
}

TEST_CASE("a path goes to UTF-8 text and back, whatever letters it holds", "[studio]") {
    // The text fields and SDL hold UTF-8; on Windows the narrow string of a path would lose the letter.
    const std::filesystem::path path = std::filesystem::path(std::u8string(u8"projects/café über/yard"));
    const std::string text = coney::studio::pathToUtf8(path);
    CHECK(text.find("caf\xC3\xA9") != std::string::npos);
    CHECK(coney::studio::pathFromUtf8(text) == path);
    CHECK(coney::studio::pathFromUtf8("").empty());
}
