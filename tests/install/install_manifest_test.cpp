// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/install_manifest.h"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/sha1.h"

using coney::install::fileType;
using coney::install::ManifestFile;
using coney::install::Platform;

TEST_CASE("a PS2 disc's files are counted by type", "[install]") {
    CHECK(fileType(Platform::Ps2, "WARRIORS.WAD") == "archive");
    CHECK(fileType(Platform::Ps2, "WARRIORS.DIR") == "archive");
    CHECK(fileType(Platform::Ps2, "IOP/BFW.SND") == "sound");
    CHECK(fileType(Platform::Ps2, "PSS/LOGO.BIK") == "movies");
    CHECK(fileType(Platform::Ps2, "MODULES/PADMAN.IRX") == "modules");
    CHECK(fileType(Platform::Ps2, "SLUS_212.15") == "executable");
    CHECK(fileType(Platform::Ps2, "SYSTEM.CNF") == "system");
    CHECK(fileType(Platform::Ps2, "IOPX") == "other");
}

TEST_CASE("a list digest is the SHA-1 of one line per file", "[install]") {
    const std::vector<ManifestFile> files{{Platform::Ps2, "A.BIN", 3, "aa"}, {Platform::Ps2, "B.BIN", 4, "bb"}};
    coney::Sha1 sha;
    sha.update("A.BIN 3 aa\nB.BIN 4 bb\n");
    CHECK(coney::install::listDigest(files) == coney::toHex(sha.finish()));
    const std::vector<ManifestFile> reordered{files[1], files[0]};
    CHECK(coney::install::listDigest(reordered) != coney::install::listDigest(files));
}

TEST_CASE("the manifest lists sources, types and files, with paths escaped", "[install]") {
    const coney::install::KnownDisc disc{"ps2-test", "A test disc", Platform::Ps2, "SLUS_212.15", {}};
    const coney::install::Manifest manifest{"1.2.3",
                                            {{&disc, "C:\\Discs\\warriors.iso", true}},
                                            {{Platform::Ps2, "SLUS_212.15", 10, "11"},
                                             {Platform::Ps2, "IOP/BFW.SND", 20, "22"},
                                             {Platform::Ps2, "IOP/MUSIC.SND", 30, "33"}},
                                            {}};
    const std::string json = coney::install::manifestJson(manifest);
    CHECK(json.find("\"format\": 1,") != std::string::npos);
    CHECK(json.find("\"path\": \"C:\\\\Discs\\\\warriors.iso\", \"kind\": \"image\"") != std::string::npos);
    CHECK(json.find("\"ps2/sound\": {\"files\": 2, \"bytes\": 50,") != std::string::npos);
    CHECK(json.find("\"ps2/executable\": {\"files\": 1, \"bytes\": 10,") != std::string::npos);
    CHECK(json.find("{\"path\": \"game/ps2/IOP/MUSIC.SND\", \"size\": 30, \"sha1\": \"33\"}") != std::string::npos);
    CHECK(json.find("\"program\": []") != std::string::npos);
    CHECK(json.ends_with("}\n"));
}
