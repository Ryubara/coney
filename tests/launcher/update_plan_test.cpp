// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher/update_plan.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::launcher::InstalledFile;
using coney::launcher::parseVersion;
using coney::launcher::planUpdate;
using coney::launcher::Release;
using coney::launcher::ReleaseFile;
using coney::launcher::UpdatePlan;

namespace {

// A digest filled with one byte, standing for a file's contents.
coney::Sha256Digest digest(std::uint8_t fill) {
    coney::Sha256Digest d{};
    d.fill(fill);
    return d;
}

// Release 0.2.0 for windows-x64 with three files.
Release release() {
    Release r;
    r.version = *parseVersion("0.2.0");
    r.minLauncher = *parseVersion("0.1.0");
    r.channel = "stable";
    r.platforms["windows-x64"] = {ReleaseFile{"coney.exe", 100, digest(1), "coney.exe"},
                                  ReleaseFile{"licences/GPL.txt", 10, digest(2), "GPL.txt"},
                                  ReleaseFile{"assets/sandbox/a.layout", 5, digest(3), "a.layout"}};
    return r;
}

} // namespace

TEST_CASE("an update downloads only what changed and removes what the release dropped", "[launcher]") {
    const std::vector<InstalledFile> installed{
        {"coney.exe", 90, digest(9)}, {"licences/GPL.txt", 10, digest(2)}, {"old-tool.exe", 7, digest(7)}};
    const auto plan = planUpdate(release(), "windows-x64", installed, *parseVersion("0.1.0"), *parseVersion("0.1.0"));
    CHECK(plan.verdict == UpdatePlan::Verdict::Ready);
    REQUIRE(plan.download.size() == 2);
    CHECK(plan.download[0].path == "coney.exe");
    CHECK(plan.download[1].path == "assets/sandbox/a.layout");
    CHECK(plan.downloadBytes == 105U);
    CHECK(plan.remove == std::vector<std::string>{"old-tool.exe"});
}

TEST_CASE("an update is refused when it cannot or need not run", "[launcher]") {
    const std::vector<InstalledFile> none;
    CHECK(planUpdate(release(), "macos-arm64", none, *parseVersion("0.1.0"), *parseVersion("0.1.0")).verdict ==
          UpdatePlan::Verdict::NoPlatform);
    Release needsNewer = release();
    needsNewer.minLauncher = *parseVersion("0.2.0");
    CHECK(planUpdate(needsNewer, "windows-x64", none, *parseVersion("0.1.0"), *parseVersion("0.1.5")).verdict ==
          UpdatePlan::Verdict::LauncherTooOld);
    CHECK(planUpdate(release(), "windows-x64", none, *parseVersion("0.2.0"), *parseVersion("0.1.0")).verdict ==
          UpdatePlan::Verdict::NotNewer);
    CHECK(planUpdate(release(), "windows-x64", none, *parseVersion("0.3.0"), *parseVersion("0.1.0")).verdict ==
          UpdatePlan::Verdict::NotNewer);
}

TEST_CASE("a repair restores the installed version's files", "[launcher]") {
    const std::vector<InstalledFile> intact{
        {"coney.exe", 100, digest(1)}, {"licences/GPL.txt", 10, digest(2)}, {"assets/sandbox/a.layout", 5, digest(3)}};
    const auto fine =
        planUpdate(release(), "windows-x64", intact, *parseVersion("0.2.0"), *parseVersion("0.1.0"), true);
    CHECK(fine.verdict == UpdatePlan::Verdict::UpToDate);
    std::vector<InstalledFile> damaged = intact;
    damaged[2].sha256 = digest(0);
    const auto repair =
        planUpdate(release(), "windows-x64", damaged, *parseVersion("0.2.0"), *parseVersion("0.1.0"), true);
    CHECK(repair.verdict == UpdatePlan::Verdict::Ready);
    REQUIRE(repair.download.size() == 1);
    CHECK(repair.download[0].path == "assets/sandbox/a.layout");
}

TEST_CASE("an update matches installed files case aside, as Windows and macOS do", "[launcher]") {
    // The install spells two files differently in case: they are the release's files, not ones it dropped, so nothing
    // is removed (removing `Coney.EXE` there would delete the `coney.exe` just downloaded).
    const std::vector<InstalledFile> installed{
        {"Coney.EXE", 90, digest(9)}, {"Licences/GPL.txt", 10, digest(2)}, {"assets/sandbox/a.layout", 5, digest(3)}};
    const auto plan = planUpdate(release(), "windows-x64", installed, *parseVersion("0.1.0"), *parseVersion("0.1.0"));
    CHECK(plan.verdict == UpdatePlan::Verdict::Ready);
    REQUIRE(plan.download.size() == 1);
    CHECK(plan.download[0].path == "coney.exe");
    CHECK(plan.remove.empty());
}
