# Recon: The Warriors (PS2, NTSC-U)

Date: 2026-10-04. Disc: `Warriors, The (USA) (En,Fr,De,Es,It).iso` (4,295,917,568 bytes).

## Disc layout

| Path | Size | Notes |
| --- | --- | --- |
| `SYSTEM.CNF` | 57 | `BOOT2 = cdrom0:\SLUS_212.15;1`, `VER = 1.03`, `VMODE = NTSC` |
| `SLUS_212.15` | 4,903,976 | Main EE ELF. SHA1 `E9CB2CC49AA046B9E494313DCE2F5038ED17B2F4` |
| `WARRIORS.DIR` | 128,428 | Index for the WAD (see below) |
| `WARRIORS.WAD` | 1,495,371,776 | All game assets, 10,701 entries |
| `IOP/BFW.SND`, `IOP/MUSIC.SND` | 1.4 GB / 960 MB | Sound banks / streamed music (IOP side) |
| `PSS/*.BIK` | 15 files | Bink videos (intro/outro per level, logos, trailer) |
| `MODULES/*.IRX`, `IOPRP300.IMG` | | Standard SCE IOP modules + one custom `IOP.IRX` (audio driver, probably) |

## Main ELF (`SLUS_212.15`)

* Entry `0x100008`; one PT_LOAD at `0x100000` (filesz `0x496f6c`, memsz `0x615b5c`).
* **71 section headers present, symbols stripped** (no `.symtab`).
    * `.text` `0x100000` (size `0x3f6578`, ~4 MB code), `.vutext` `0x4f6580` (VU microcode, `0x14290`),
      `.data` `0x50a880`, `.rodata` `0x546480`, `.gcc_except_table`, `.lit4`, `.sdata`, `.sbss`,
      `.bss` `0x597200` (~1.5 MB).
    * ~60 `.DVP.overlay..*` sections: VU microprograms (RenderWare PS2 pipelines + custom).
* **Compiler: GCC (SN/Sony ee-gcc)** — `.gcc_except_table` (C++ exceptions on), `.reginfo`, DVP sections.
  No GCC RTTI type-name strings found → likely `-fno-rtti`. Exact GCC version still to confirm (prologue/codegen idioms).
* SCE SDK 3.0.0 (`PsIIlibkernl3000`, `PsIIlibgraph3000`).
* **152 source paths embedded** (`c:/Warriors/Source/<Subsystem>/<File>.cpp`, from asserts/debug strings).
  This gives the original source tree and file names, so functions can be grouped into their real translation units:
  the [source map](source-map.md) does this for all 153 paths (152 `.cpp` plus one `.inl`) and the middleware.
  Subsystems: Animation, Audio, Camera, Core (ChunkSystem), Debug, Device/ps2 (fileio, memorycard, sound, Shell),
  FileIO, GameModes, Graphics (+ Devices/Renderware, OverlayEffects), GUI (+ RumbleModeGUI, ProfileManagementGUI),
  Human (+ pathfinding, cns), Memory, Physics, RayCast, Scene, Scripting, StringTable, TaskEngine, Utils, World(+ps2),
  WorldObjects, Warriors, Movie. Note `Gm_XboxSaveSystem.cpp` → shared codebase with the Xbox port.
* Middleware: **RenderWare Graphics** (`DevRWGeneric.cpp`, "Renderware Texture Dic"), **Lua 4.0.1** + **tolua**
  (`c:/Warriors/System/tolua`), **Bink** video, SCE libmc/libpad/libmtap.
* Engine: in-house "Warriors" engine using RenderWare as the graphics device — **not** the GTA3-era RW game framework,
  so re3 code is a reference for RW usage only, not for game logic.

Full string dump: `game/slus_strings.txt` (local only, 11,061 strings).

## `WARRIORS.DIR` / `WARRIORS.WAD`

```text
DIR:  u32 count (=10701); u8 pad[12];
      struct { u32 wadOffset; u32 size; u32 nameHash; } entries[count];   // sorted by offset, 2048-aligned
```

16 + 10701×12 = 128,428 = file size exactly. Names are stored only as hashes. Not CRC32/FNV/djb/sdbm/OAAT of bare
filenames → get the hash from `DVDWadIndexPS2.cpp` / `RockWadIndexPS2.cpp` code (TODO).

Entry content by leading bytes (counts of 10,701):

| Count | Lead | Guess |
| --- | --- | --- |
| 4771 | `01 00 00 00`, size, 0, ? | **Texture dictionary**: `{u32 type=1, u32 size, u32 0, u32 ?}` → inner chunk `0x2A` (same header) → RW stream `0x16` rwID_TEXDICTIONARY, RW version stamp `0x1C02000A`, PS2-native palettized textures (librw can read these) |
| 1461 | `02 00 00 00` … | chunk container type 2 (larger, ~256 KB) |
| 467 | `1B 4C 75 61 40` | **Compiled Lua 4.0 bytecode** — game scripts; decompilable with a Lua 4.0 decompiler. Source names stripped (`=(none)`) |
| ~600 | types 3–0x46 | other chunk types |
| ~200 | `<size> 00 00 00 00 <ascii name>` | named records, e.g. `gen_cop0`, `l14_l1_0`, `cam_audi` (likely anim/cutscene data) |
| 26 | `0\r\n` | tiny text |
| 18 | `PI\0\0` | ? |
| 13+5 | `24`/`1E 00 00 00 16 00 00 00 … 0A 00 02 1C` | RenderWare binary stream (version-tagged), likely world/BSP |

## Implications for Coney

1. Lua scripts carry much of the mission/game logic → decompile bytecode instead of MIPS for that part.
2. Source file list → Ghidra can be organised by TU from assert-string xrefs (done: [Source map](source-map.md)).
3. Asset pipeline: write a WAD extractor + chunk-format docs early; RW streams can be loaded by **librw** on PC.
4. VU microcode/DVP overlays do not need decompiling — replaced by librw's PC renderer.

## Status

* Ghidra: ELF imported into `research/ghidra/warriors` with ElfLoader + R5900 (auto-analysis).
