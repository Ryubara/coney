// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/known_discs.h"

#include <array>

namespace coney::install {

namespace {

// The NTSC-U PlayStation 2 disc (SLUS-21215): all 29 files, as a mounted disc and as an ISO image of it list them.
// Read with Python's hashlib on 2026-10-20, from the mounted disc and from the image, which agree; the boot
// executable's SHA-1 is the one python/src/coney_tools/extract.py keys its expected counts by.
// docs/research/discs.md#ps2-ntsc-u
constexpr std::array kPs2NtscU = std::to_array<KnownFile>({
    {"IOP/BFW.SND", 1'402'861'568, "da8f073e0339e06ae455a68626549a3db88dd75c"},
    {"IOP/MUSIC.SND", 960'063'488, "aac2dcccd410b9071c75e18d48b49e1b6843d4c5"},
    {"MODULES/IOP.IRX", 143'132, "8cf097b41428bc530ca5ec17f57d006fe2342353"},
    {"MODULES/IOPRP300.IMG", 275'345, "51fe51e904bb4ef87e996b8a5062aaeb46d53b31"},
    {"MODULES/LIBSD.IRX", 28'661, "595db0576397dc2e4ea41d3ac56b1684c76cbb28"},
    {"MODULES/MCMAN.IRX", 95'909, "dd446317f60632dc22f45a92fb38ff6a76ae8210"},
    {"MODULES/MCSERV.IRX", 7'417, "c406a86684514c3d47475dc8b741b52d539dff7c"},
    {"MODULES/PADMAN.IRX", 44'869, "77752afc1e029c5fe89c8c60462664b61963fe8e"},
    {"MODULES/SIO2MAN.IRX", 6'641, "2eeb68eaf5b73659be9d4986578b0caadaafe484"},
    {"PSS/L1_IN.BIK", 58'558'968, "b66a8139faf58cddc51c8cbf5aad3b9aad253f52"},
    {"PSS/L31_IN.BIK", 16'767'680, "c57d8c24eb982e9fbb619afb7814c281d611410a"},
    {"PSS/L31_OUT.BIK", 19'479'396, "7ea13c8fc756dd549f2b18f9c7465978bb7d3b78"},
    {"PSS/L34_OUT.BIK", 22'788'160, "e24208fc14e5ad09e10d216c926f33327231c3d1"},
    {"PSS/L51_IN.BIK", 140'089'108, "aa33b3a9366133cd67a6ac3e15f1a5e4ef3bbdad"},
    {"PSS/L52_IN.BIK", 22'345'988, "895c7ac91d0c619631dddd5e2915f3385492f577"},
    {"PSS/L54_IN.BIK", 15'270'360, "07589de172123ee930be3616d1496c026acf7b40"},
    {"PSS/L81_OUT.BIK", 11'607'724, "6e41cb1ffa209c3a5ead07904e31957bd1fc412f"},
    {"PSS/L84_OUT.BIK", 139'516, "0ad8594e80c92ce8e50a32821860afde884ca339"},
    {"PSS/L87_OUT.BIK", 12'819'700, "0484ddb662833eba4b22e34d82dd3fa6d8f749fc"},
    {"PSS/L99_IN.BIK", 21'897'112, "568990ff50a67f70d6f6e1cfef3bb46d76d8f1d2"},
    {"PSS/L9_IN.BIK", 34'138'400, "2de7e4527d02a3e8b1575f0ecbb2c066f77fcb26"},
    {"PSS/L9_OUT.BIK", 4'933'168, "8a51325065876bd33e2220f840db46f002f824c7"},
    {"PSS/LOGO.BIK", 303'228, "4a1068914375f897dee04fc21db8ca3120b7a3e1"},
    {"PSS/PLOGO.BIK", 3'560'952, "2a2f869c2fac0ba82d712ba514e6e97fd5967573"},
    {"PSS/TRAILER.BIK", 25'638'616, "01ea34f978e9e1e3dd44ec239b68f4ff4b59fdba"},
    {"SLUS_212.15", 4'903'976, "e9cb2cc49aa046b9e494313dce2f5038ed17b2f4"},
    {"SYSTEM.CNF", 57, "92103edb6a12a1f838f996e2271ba54174159342"},
    {"WARRIORS.DIR", 128'428, "0170cf199ed10947f8d20ecd40a5f4b0bd7877f7"},
    {"WARRIORS.WAD", 1'495'371'776, "f286eb7c4097fcb55e2b9c5ec882923d2694060f"},
});

constexpr std::array kKnownDiscs = std::to_array<KnownDisc>({
    {"ps2-ntsc-u", "The Warriors (PlayStation 2, NTSC-U, SLUS-21215)", Platform::Ps2, "SLUS_212.15", kPs2NtscU},
});

} // namespace

std::span<const KnownDisc> knownDiscs() { return kKnownDiscs; }

} // namespace coney::install
