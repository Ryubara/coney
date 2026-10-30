// SPDX-License-Identifier: GPL-3.0-or-later
// `coney --script-patch make|apply` (docs/guides/script-patches.md#command-line), on made-up files in a temporary
// folder.
#include "platform/script_patch_command.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/options.h"
#include "core/script_patch.h"
#include "support/fixtures.h"

using coney::test::TempDir;

namespace {

// A temporary file for the command's output. MSVC deprecates std::tmpfile (C4996, an error in the ci preset), so it
// gets tmpfile_s, which opens the same kind of file.
std::FILE* scratchFile() {
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    return tmpfile_s(&file) == 0 ? file : nullptr;
#else
    return std::tmpfile();
#endif
}

// Everything written to `file`, which is then closed.
std::string drain(std::FILE* file) {
    REQUIRE(std::fseek(file, 0, SEEK_SET) == 0);
    std::string text;
    for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file)) {
        text.push_back(static_cast<char>(c));
    }
    std::fclose(file);
    return text;
}

// What one run printed and returned.
struct Run {
    int code = 0;
    std::string out;
    std::string err;
};

// Runs the command with `args` (those after `--script-patch`).
Run run(const std::vector<std::string>& args) {
    std::vector<std::string_view> views(args.begin(), args.end());
    std::FILE* out = scratchFile();
    std::FILE* err = scratchFile();
    REQUIRE(out != nullptr);
    REQUIRE(err != nullptr);
    Run result;
    result.code = coney::platform::runScriptPatchCommand(views, out, err);
    result.out = drain(out);
    result.err = drain(err);
    return result;
}

// Writes `text` to `name` in `dir` and returns its path as a string.
std::string writeText(const TempDir& dir, std::string_view name, std::string_view text) {
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    return dir.write(name, bytes).string();
}

// The whole of a file, as bytes.
std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

constexpr std::string_view kBase =
    "-- a made-up script\nlocal count = 5\nprint(\"the count is\", count)\nreturn count\n";
constexpr std::string_view kEdited =
    "-- a made-up script\nlocal count = 6\nprint(\"the count is\", count)\nConey.Log(\"edited\")\nreturn count\n";
const std::string kChunk = "sha256:" + std::string(64, 'a');

} // namespace

TEST_CASE("coney --script-patch make and apply give back the edited file", "[patch]") {
    // Expected: ../guides/script-patches.md#command-line, Coney's own format (no counterpart in the original)
    TempDir dir;
    const std::string base = writeText(dir, "base.lua", kBase);
    const std::string edited = writeText(dir, "edited.lua", kEdited);
    const std::string patch = (dir.path() / "yard.cpatch").string();
    const std::string result = (dir.path() / "result.lua").string();

    const Run made = run({"make", base, edited, "--out", patch, "--chunk", kChunk, "--name", "yard.lua"});
    CAPTURE(made.err);
    REQUIRE(made.code == 0);
    CHECK(made.out.find("1 rebuilt") != std::string::npos);
    CHECK(made.out.find("1 added") != std::string::npos);
    const std::string written = readText(patch);
    CHECK(written.starts_with("coney-script-patch 1\n"));
    CHECK(written.find("name   = \"yard.lua\"") != std::string::npos);
    CHECK(written.find("local count") == std::string::npos);

    const Run applied = run({"apply", patch, base, "--out", result});
    CAPTURE(applied.err);
    REQUIRE(applied.code == 0);
    CHECK(readText(result) == kEdited);
    CHECK(applied.out.find(coney::patch::hashText(coney::patch::textHash(kEdited))) != std::string::npos);
}

TEST_CASE("coney --script-patch make takes the chunk's hash from the chunk's file", "[patch]") {
    // Expected: ../guides/script-patches.md#command-line, Coney's own format (no counterpart in the original)
    TempDir dir;
    const std::string chunk = writeText(dir, "yard.luac", "made-up chunk bytes");
    const std::string patch = (dir.path() / "yard.cpatch").string();
    const Run made = run({"make", writeText(dir, "base.lua", kBase), writeText(dir, "edited.lua", kEdited), "--out",
                          patch, "--chunk-file", chunk, "--entry", "0x1c2d3e4f"});
    CAPTURE(made.err);
    REQUIRE(made.code == 0);
    const std::string written = readText(patch);
    CHECK(written.find(coney::patch::hashText(coney::patch::textHash("made-up chunk bytes"))) != std::string::npos);
    CHECK(written.find("entry  = 0x1c2d3e4f") != std::string::npos);
}

TEST_CASE("coney --script-patch apply refuses a base the patch was not made from", "[patch]") {
    // Expected: ../guides/script-patches.md#command-line, Coney's own format (no counterpart in the original)
    TempDir dir;
    const std::string patch = (dir.path() / "yard.cpatch").string();
    REQUIRE(run({"make", writeText(dir, "base.lua", kBase), writeText(dir, "edited.lua", kEdited), "--out", patch,
                 "--chunk", kChunk})
                .code == 0);
    const std::string other = writeText(dir, "other.lua", "-- another script\n");
    const std::string result = (dir.path() / "result.lua").string();
    const Run applied = run({"apply", patch, other, "--out", result});
    CHECK(applied.code == 1);
    CHECK(applied.err.find("no section of the patch is for this text") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(result));
}

TEST_CASE("coney --script-patch refuses a bad command line with its usage", "[patch]") {
    // Expected: ../guides/script-patches.md#command-line, Coney's own format (no counterpart in the original)
    TempDir dir;
    const std::string base = writeText(dir, "base.lua", kBase);
    for (const auto& args : std::vector<std::vector<std::string>>{
             {},
             {"merge", "a", "b"},
             {"make", base, base},
             {"make", base, base, "--out", "x.cpatch"},
             {"make", base, base, "--out", "x.cpatch", "--chunk", "sha256:abc"},
             {"make", base, base, "--out", "x.cpatch", "--chunk", kChunk, "--entry", "12"},
             {"apply", "x.cpatch", base},
             {"apply", "x.cpatch", base, "--out", "y", "--out", "z"},
         }) {
        CAPTURE(args);
        const Run bad = run(args);
        CHECK(bad.code == 2);
        CHECK(bad.err.find("Usage: coney --script-patch") != std::string::npos);
    }
}

TEST_CASE("coney --script-patch says which file it cannot read", "[patch]") {
    // Expected: ../guides/script-patches.md#command-line, Coney's own format (no counterpart in the original)
    TempDir dir;
    const std::string missing = (dir.path() / "missing.lua").string();
    const Run made = run({"make", missing, missing, "--out", (dir.path() / "x.cpatch").string(), "--chunk", kChunk});
    CHECK(made.code == 1);
    CHECK(made.err.find("missing.lua") != std::string::npos);
}

TEST_CASE("the usage text of both builds names coney --script-patch", "[options]") {
    // Expected: ../guides/script-patches.md#command-line, Coney's own format (no counterpart in the original)
    // The command line for script patches runs before the options are read, so the usage text is where a modder
    // finds it.
    CHECK(coney::usageText().find("coney --script-patch make|apply") != std::string_view::npos);
    CHECK(coney::usageText(coney::OptionSet::Player).find("coney --script-patch make|apply") != std::string::npos);
}
