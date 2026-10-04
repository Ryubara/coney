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

## Coney's implementation

`python/src/coney_tools/wad.py` parses `WARRIORS.DIR` (`parse_dir`), hashes names (`name_hash`), extracts entries
and recovers names; `disc.py` reads the files from a folder or an ISO 9660 image; `wad_cli.py` holds the
`coney-tools wad` commands. How to run them: [The coney-tools command line](../../guides/coney-tools.md).

Name recovery (`coney-tools wad names`) scans printable file-name-like strings (path characters followed by one or
more `.ext` parts) in `SLUS_212.15` and in every WAD entry, hashes each as `./ee_files/<last path component>` and
as written, and keeps those that match an entry's hash. It recovered 414 of 10,701 names on the NTSC-U disc
(2026-10-04). The parser rejects a `WARRIORS.DIR` whose size is not `16 + count x 12` and an entry that ends past
the end of the WAD; the game itself does neither check as far as these pages record.

## Open questions

- Is the whole disc one flat root (`SLUS_212.15`, `WARRIORS.DIR`, `WARRIORS.WAD` and the other files), or does the
  game read any file from a subdirectory? The ISO reader only reads the root directory.
- Do names ever hash without the `./ee_files/` prefix, for example names built by other callers of the hash? The
  recovery tries both forms, but this page only documents the prefixed one.
- Why do roughly 96% of the entries have no name in any string on the disc? Are the names built at run time
  (for example from a number or a level name), or stored in a form the string scan does not see?
