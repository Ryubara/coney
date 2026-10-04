# WARRIORS.DIR / WARRIORS.WAD

The game's assets live in one archive, `WARRIORS.WAD` (1,495,371,776 bytes on the NTSC-U disc), indexed by
`WARRIORS.DIR` (128,428 bytes). Loaded by `DVDWadIndex` (`c:/Warriors/Source/Device/ps2/fileio/DVDWadIndexPS2.cpp`).

## WARRIORS.DIR

All values little-endian.

```c
struct WadDirHeader {
    uint32_t count;      // 10701 on NTSC-U
    uint8_t  pad[12];    // zero; the game seeks to 0x10 before reading entries
};

struct WadDirEntry {     // 12 bytes, `count` of them, sorted by wadOffset
    uint32_t wadOffset;  // byte offset into WARRIORS.WAD, 2048-aligned
    uint32_t size;       // byte size
    uint32_t nameHash;   // CRC32 of the lowercased path, see below
};
```

Check: `16 + 10701 × 12 = 128,428` = file size.

**Confidence:** confirmed from code. The constructor at `0x149160` reads `count`, seeks to `0x10`, allocates
`count * 12` bytes and reads the entry table.

## Name lookup

`DVDWadIndex::Find` (`0x1490b8`):

1. Lowercase the name in place (`0x143fd8`).
2. `hash = crc32(name)` (`0x143f68`). This is standard CRC-32, see [Name hashing](../name-hash.md).
3. Linear scan of the entries for `nameHash == hash`.

Names are paths of the form **`./ee_files/<basename>`**, built by `"./ee_files/" + name` at `0x148aa0` / `0x148ba8`.
The folder is flat: there are no subdirectories under `ee_files/`.

Example: `./ee_files/global.lua` → `0x7e23a6f2`.

**Recovered names:** 412 of 10,701 so far (2026-10-04), from strings in the ELF and inside WAD files.

## Entry contents

See [Recon](../overview.md#warriorsdir-warriorswad) for the first classification by leading bytes.
Per-type format pages will be added as each type is decoded.
