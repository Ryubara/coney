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

16 + 10701×12 = 128,428 = file size exactly. Names are stored only as hashes: CRC-32 of the lowercased
`./ee_files/<name>` path, see [Name hashing](name-hash.md) and [WARRIORS.DIR / .WAD](formats/wad-dir.md).

What the entries are is surveyed on [WAD contents](formats/wad-contents.md). In short: about half of the archive
(5,127 entries) uses one chunk container of packs, resources and typed chunks, holding RenderWare texture
dictionaries, models and worlds, animations, characters and level data; 2,765 entries are scene records, 2,229 are
a level's streamed world (RenderWare atomics, worlds and their manifests), 467 are Lua 4.0 bytecode, and the rest are
sound banks, object lists and a few singletons.

The leading word of a chunk container is its chunk (or group) count, not a type. The layout and the loaders are on
[Chunk system](chunk-system.md#container-layout).

## Implications for Coney

1. Lua scripts carry much of the mission/game logic → decompile bytecode instead of MIPS for that part.
2. Source file list → Ghidra can be organised by TU from assert-string xrefs (done: [Source map](source-map.md)).
3. Asset pipeline: write a WAD extractor + chunk-format docs early ([Chunk system](chunk-system.md)); RW streams can
   be loaded by **librw** on PC.
4. VU microcode/DVP overlays do not need decompiling — replaced by librw's PC renderer.
5. Engine core: how the game boots and runs its frame is on [Boot and the main loop](boot.md); how it reads files,
   on [File I/O](file-io.md).

## Status

* Ghidra: ELF imported into `research/ghidra/warriors` with ElfLoader + R5900 (auto-analysis).
