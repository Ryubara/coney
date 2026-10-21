# The discs Coney installs from

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); read from the mounted disc
and from its image on 2026-10-20.

## Purpose

Coney plays from the player's own disc, copied once into an install folder
([The install folder](../guides/asset-and-script-architecture.md#install-folder)). To copy it the installer must know
which disc it was given and that every file arrived whole. This page is that reference: the releases Coney accepts,
every file of each with its size and SHA-1, and how the installer tells a wrong disc from a damaged one. It is a curated
reference list as [LEGAL.md](repo:LEGAL.md#no-game-data) allows: names, sizes and SHA-1s, never the files.

## Original structure

Nothing in the game reads these lists: the console boots the executable `SYSTEM.CNF` names
(`BOOT2 = cdrom0:\SLUS_212.15;1`, [Recon](overview.md#disc-layout)), and the game opens `WARRIORS.DIR` and
`WARRIORS.WAD` from the disc ([File I/O](file-io.md#opening-the-wad-at-boot)).

## Data

### The PS2 disc, NTSC-U (SLUS-21215) {#ps2-ntsc-u}

Every file on the disc: the root, and the folders `IOP/`, `MODULES/` and `PSS/` directly below it. The mounted disc
and its ISO image (`Warriors, The (USA) (En,Fr,De,Es,It).iso`, 4,295,917,568 bytes, image SHA-1
`3b68f15cc0c831597467b477ecd9c0944b08c17f`) list the same 29 files with the same SHA-1s. Confirmed (runtime: every
file read whole and hashed, 2026-10-20). The type is the one `index/install.json` counts the file under.

| File | Type | Bytes | SHA-1 |
| --- | --- | ---: | --- |
| `IOP/BFW.SND` | sound | 1,402,861,568 | `da8f073e0339e06ae455a68626549a3db88dd75c` |
| `IOP/MUSIC.SND` | sound | 960,063,488 | `aac2dcccd410b9071c75e18d48b49e1b6843d4c5` |
| `MODULES/IOP.IRX` | modules | 143,132 | `8cf097b41428bc530ca5ec17f57d006fe2342353` |
| `MODULES/IOPRP300.IMG` | modules | 275,345 | `51fe51e904bb4ef87e996b8a5062aaeb46d53b31` |
| `MODULES/LIBSD.IRX` | modules | 28,661 | `595db0576397dc2e4ea41d3ac56b1684c76cbb28` |
| `MODULES/MCMAN.IRX` | modules | 95,909 | `dd446317f60632dc22f45a92fb38ff6a76ae8210` |
| `MODULES/MCSERV.IRX` | modules | 7,417 | `c406a86684514c3d47475dc8b741b52d539dff7c` |
| `MODULES/PADMAN.IRX` | modules | 44,869 | `77752afc1e029c5fe89c8c60462664b61963fe8e` |
| `MODULES/SIO2MAN.IRX` | modules | 6,641 | `2eeb68eaf5b73659be9d4986578b0caadaafe484` |
| `PSS/L1_IN.BIK` | movies | 58,558,968 | `b66a8139faf58cddc51c8cbf5aad3b9aad253f52` |
| `PSS/L31_IN.BIK` | movies | 16,767,680 | `c57d8c24eb982e9fbb619afb7814c281d611410a` |
| `PSS/L31_OUT.BIK` | movies | 19,479,396 | `7ea13c8fc756dd549f2b18f9c7465978bb7d3b78` |
| `PSS/L34_OUT.BIK` | movies | 22,788,160 | `e24208fc14e5ad09e10d216c926f33327231c3d1` |
| `PSS/L51_IN.BIK` | movies | 140,089,108 | `aa33b3a9366133cd67a6ac3e15f1a5e4ef3bbdad` |
| `PSS/L52_IN.BIK` | movies | 22,345,988 | `895c7ac91d0c619631dddd5e2915f3385492f577` |
| `PSS/L54_IN.BIK` | movies | 15,270,360 | `07589de172123ee930be3616d1496c026acf7b40` |
| `PSS/L81_OUT.BIK` | movies | 11,607,724 | `6e41cb1ffa209c3a5ead07904e31957bd1fc412f` |
| `PSS/L84_OUT.BIK` | movies | 139,516 | `0ad8594e80c92ce8e50a32821860afde884ca339` |
| `PSS/L87_OUT.BIK` | movies | 12,819,700 | `0484ddb662833eba4b22e34d82dd3fa6d8f749fc` |
| `PSS/L99_IN.BIK` | movies | 21,897,112 | `568990ff50a67f70d6f6e1cfef3bb46d76d8f1d2` |
| `PSS/L9_IN.BIK` | movies | 34,138,400 | `2de7e4527d02a3e8b1575f0ecbb2c066f77fcb26` |
| `PSS/L9_OUT.BIK` | movies | 4,933,168 | `8a51325065876bd33e2220f840db46f002f824c7` |
| `PSS/LOGO.BIK` | movies | 303,228 | `4a1068914375f897dee04fc21db8ca3120b7a3e1` |
| `PSS/PLOGO.BIK` | movies | 3,560,952 | `2a2f869c2fac0ba82d712ba514e6e97fd5967573` |
| `PSS/TRAILER.BIK` | movies | 25,638,616 | `01ea34f978e9e1e3dd44ec239b68f4ff4b59fdba` |
| `SLUS_212.15` | executable | 4,903,976 | `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4` |
| `SYSTEM.CNF` | system | 57 | `92103edb6a12a1f838f996e2271ba54174159342` |
| `WARRIORS.DIR` | archive | 128,428 | `0170cf199ed10947f8d20ecd40a5f4b0bd7877f7` |
| `WARRIORS.WAD` | archive | 1,495,371,776 | `f286eb7c4097fcb55e2b9c5ec882923d2694060f` |

29 files, 4,274,269,343 bytes.

The boot executable is what names the release: another release (another region, a later printing) has another
`SLUS_*`/`SLES_*` file or the same name with other contents. Only the NTSC-U disc is known; it is the disc every
research page and every test is checked against.

### The digests in `index/install.json` {#digests}

An install records, per disc and per type of file, a digest: the SHA-1 over one line `<name> <size> <sha1>\n` per file,
in the order of the table above (`install::listDigest`). Two installs from the same disc have the same digests,
whatever the folder or the source (mounted disc or image), so a digest says whether two players have the same files
without either sending one.

## Behaviour

How the installer (`coney --install`, [Running Coney](../guides/running.md#install)) uses the list, in order:

1. **Which disc.** It lists the disc's files with their sizes and looks for a known release whose key file (the boot
   executable) is there with its size. With none, it says the disc is not The Warriors (no `WARRIORS.WAD` and no
   known boot file) or is another release (`WARRIORS.WAD` without the known boot file, or the boot file of another
   size), and names the releases it takes.
2. **Is it whole.** Every file of that release must be there with its size; a missing one is "an incomplete copy", a
   size that differs "a damaged or modified copy". Nothing is written before both checks pass.
3. **Did it copy whole.** Each file's SHA-1 is computed while it is copied and compared with the table; a file that
   differs is not kept, and the install stops with the file's name and both SHA-1s.

## Coney's implementation

The list is compiled into Coney, so the installer needs no repository and no network:
`src/install/known_discs.cpp` holds the table above (`install::knownDiscs()`); `src/install/installer.cpp` does the
three steps; `src/core/sha1.cpp` is Coney's own SHA-1. The disc test `disc_install_test.cpp` checks the player's disc
against the list on every disc run (sizes for every file, SHA-1s for the files under 8 MB; the large ones are hashed by
a full install). A change to the table is a change to this page in the same commit.

## Open questions

1. **Other releases.** The PAL and Japanese discs, and any later NTSC-U printing, are not listed: their files, sizes
   and SHA-1s are unknown, and so is whether their executable differs from `SLUS_212.15` in code (which every address
   in this documentation assumes). A player with one gets "another release"; adding it needs its list and a check of
   the executable against the research.
2. **The Xbox disc.** Its archive and movies are planned as a second install source
   ([Xbox assets](xbox-assets.md#coneys-implementation)); its file list goes on this page with that unit.
